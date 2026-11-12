#include "AnalysisDialog.h"

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPdfWriter>
#include <QPushButton>
#include <QMessageBox>
#include <QVBoxLayout>

namespace {

// Standard colours for plot series.
const QColor kSeriesColors[] = {
    QColor(0x1c, 0x6b, 0xb0), QColor(0xa8, 0x2c, 0x2c), QColor(0x2d, 0x86, 0x2d),
    QColor(0xb0, 0x7d, 0x1c), QColor(0x6a, 0x3a, 0xb0), QColor(0x1c, 0x8a, 0x8a),
};

// Analysis presets. Command templates use these tokens:
//   %1 = engine binary, %2 = energy data, %3 = run input,
//   %4 = trajectory, %5 = output curve file.
// Energy presets feed explicit term names (robust across energy files);
// trajectory presets feed the group numbers chosen in the dialog.
struct Preset {
    const char *label;
    const char *command;
    const char *stdinText; // explicit selection or empty (= group numbers)
    const char *xvgOut;
    int needs;             // DataNeeds bitmask
    int groupPrompts;      // how many group selections the tool asks for
};

const Preset kPresets[] = {
    {"Energy — Potential", "%1 energy -f %2 -o %5", "Potential\n0\n", "potential.xvg",
     AnalysisDialog::NeedsEnergy, 0},
    {"Energy — Temperature & Pressure", "%1 energy -f %2 -o %5",
     "Temperature\nPressure\n0\n", "energy.xvg", AnalysisDialog::NeedsEnergy, 0},
    {"RMSD vs reference", "%1 rms -s %3 -f %4 -o %5", "", "rmsd.xvg",
     AnalysisDialog::NeedsRunInput | AnalysisDialog::NeedsTrajectory, 2},
    {"Radius of gyration", "%1 gyrate -s %3 -f %4 -o %5", "", "rg.xvg",
     AnalysisDialog::NeedsRunInput | AnalysisDialog::NeedsTrajectory, 1},
    {"Radial distribution function", "%1 rdf -s %3 -f %4 -o %5", "", "rdf.xvg",
     AnalysisDialog::NeedsRunInput | AnalysisDialog::NeedsTrajectory, 1},
    {"SASA", "%1 sasa -s %3 -f %4 -o %5", "", "sasa.xvg",
     AnalysisDialog::NeedsRunInput | AnalysisDialog::NeedsTrajectory, 1},
    {"MSD (diffusion)", "%1 msd -s %3 -f %4 -o %5", "", "msd.xvg",
     AnalysisDialog::NeedsRunInput | AnalysisDialog::NeedsTrajectory, 1},
    {"Secondary structure (DSSP)", "%1 dssp -s %3 -f %4 -o %5", "", "dssp.xvg",
     AnalysisDialog::NeedsRunInput | AnalysisDialog::NeedsTrajectory, 1},
};

} // namespace

// ---------------------------------------------------------------------------
// XvgPlotWidget
// ---------------------------------------------------------------------------

XvgPlotWidget::XvgPlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(320);
}

void XvgPlotWidget::setXvgData(const XvgData &data)
{
    m_data = data;
    update();
}

void XvgPlotWidget::clearPlot()
{
    m_data.series.clear();
    update();
}

void XvgPlotWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);

    // gather bounds
    double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
    bool any = false;
    for (const XvgSeries &s : m_data.series) {
        for (const QPointF &p : s.points) {
            if (!any) {
                xmin = xmax = p.x();
                ymin = ymax = p.y();
                any = true;
            } else {
                xmin = qMin(xmin, p.x()); xmax = qMax(xmax, p.x());
                ymin = qMin(ymin, p.y()); ymax = qMax(ymax, p.y());
            }
        }
    }
    const QRect plotRect = rect().adjusted(70, 24, -20, -46);
    if (!any) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("No curve yet — run a report above, then the newest curve\n"
                            "in the project folder is plotted here."));
        return;
    }
    if (xmax - xmin < 1e-12) xmax = xmin + 1.0;
    if (ymax - ymin < 1e-12) ymax = ymin + 1.0;
    const double padY = (ymax - ymin) * 0.08;
    ymin -= padY; ymax += padY;

    auto mapX = [&](double x) { return plotRect.left() + (x - xmin) / (xmax - xmin) * plotRect.width(); };
    auto mapY = [&](double y) { return plotRect.bottom() - (y - ymin) / (ymax - ymin) * plotRect.height(); };

    // grid + ticks
    painter.setPen(QPen(QColor(0xe2, 0xe2, 0xe2), 1));
    for (int i = 0; i <= 8; ++i) {
        const double x = xmin + (xmax - xmin) * i / 8.0;
        painter.drawLine(QPointF(mapX(x), plotRect.top()), QPointF(mapX(x), plotRect.bottom()));
    }
    for (int i = 0; i <= 5; ++i) {
        const double y = ymin + (ymax - ymin) * i / 5.0;
        painter.drawLine(QPointF(plotRect.left(), mapY(y)), QPointF(plotRect.right(), mapY(y)));
    }

    // axes labels + tick numbers
    painter.setPen(Qt::darkGray);
    painter.setFont(font());
    for (int i = 0; i <= 8; i += 2) {
        const double x = xmin + (xmax - xmin) * i / 8.0;
        painter.drawText(QRectF(mapX(x) - 40, plotRect.bottom() + 4, 80, 16),
                         Qt::AlignHCenter, QString::number(x, 'g', 4));
    }
    for (int i = 0; i <= 5; ++i) {
        const double y = ymin + (ymax - ymin) * i / 5.0;
        painter.drawText(QRectF(0, mapY(y) - 8, 62, 16),
                         Qt::AlignRight, QString::number(y, 'g', 4));
    }
    painter.setPen(Qt::black);
    painter.drawText(QRectF(plotRect.left(), plotRect.bottom() + 20, plotRect.width(), 18),
                     Qt::AlignHCenter, m_data.xlabel);
    painter.save();
    painter.translate(14, plotRect.center().y());
    painter.rotate(-90);
    painter.drawText(QRectF(-plotRect.width() / 2, -12, plotRect.width(), 18),
                     Qt::AlignHCenter, m_data.ylabel);
    painter.restore();

    // frame
    painter.setPen(QPen(Qt::black, 1.2));
    painter.drawRect(plotRect);

    // series
    painter.setClipRect(plotRect);
    for (int si = 0; si < m_data.series.size(); ++si) {
        const XvgSeries &s = m_data.series.at(si);
        if (s.points.isEmpty())
            continue;
        painter.setPen(QPen(kSeriesColors[si % 6], 1.6));
        QPainterPath path;
        path.moveTo(mapX(s.points.first().x()), mapY(s.points.first().y()));
        for (const QPointF &p : s.points)
            path.lineTo(mapX(p.x()), mapY(p.y()));
        painter.drawPath(path);
    }
    painter.setClipping(false);

    // legend
    int li = 0;
    for (const XvgSeries &s : m_data.series) {
        if (s.legend.isEmpty())
            continue;
        const QColor color = kSeriesColors[li % 6];
        painter.setPen(color);
        painter.fillRect(plotRect.right() - 190, plotRect.top() + 8 + li * 18, 14, 3, color);
        painter.drawText(QRectF(plotRect.right() - 170, plotRect.top() + 2 + li * 18, 165, 16),
                         Qt::AlignLeft, s.legend.left(26));
        ++li;
    }
}

// ---------------------------------------------------------------------------
// AnalysisDialog
// ---------------------------------------------------------------------------

AnalysisDialog::AnalysisDialog(CoreBridge *core, const QString &projectDir,
                               const QString &gmxPath, QWidget *parent)
    : QDialog(parent)
    , m_core(core)
    , m_dir(projectDir.isEmpty() ? QStringLiteral(".") : projectDir)
    , m_gmx(gmxPath)
{
    setWindowTitle(tr("Analysis Dashboard"));
    resize(960, 680);

    auto *layout = new QVBoxLayout(this);
    auto *topForm = new QFormLayout;

    m_preset = new QComboBox(this);
    for (const Preset &p : kPresets)
        m_preset->addItem(tr(p.label));
    topForm->addRow(tr("Report type:"), m_preset);

    // Data discovery: the project directory is scanned for the files each
    // report needs, so the user never types a file name.
    auto *dataRow = new QHBoxLayout;
    auto *rescanBtn = new QPushButton(tr("Re-scan folder"), this);
    dataRow->addWidget(rescanBtn);
    dataRow->addStretch(1);
    m_dataStatus = new QLabel(this);
    m_dataStatus->setStyleSheet(QStringLiteral("color:#555;"));

    m_energyFile = new QComboBox(this);
    m_runInput = new QComboBox(this);
    m_trajectory = new QComboBox(this);
    topForm->addRow(tr("Energy data:"), m_energyFile);
    topForm->addRow(tr("Run input:"), m_runInput);
    topForm->addRow(tr("Trajectory:"), m_trajectory);
    topForm->addRow(QString(), m_dataStatus);
    topForm->addRow(QString(), dataRow);
    layout->addLayout(topForm);

    // Group selection: the numbers are delivered to the running report
    // automatically, exactly like choosing groups in a terminal session.
    auto *groupRow = new QHBoxLayout;
    m_groupA = new QComboBox(this);
    m_groupB = new QComboBox(this);
    auto *reloadGroups = new QPushButton(tr("Reload selections"), this);
    groupRow->addWidget(new QLabel(tr("Group 1:")), 0);
    groupRow->addWidget(m_groupA, 1);
    groupRow->addWidget(new QLabel(tr("Group 2:")), 0);
    groupRow->addWidget(m_groupB, 1);
    groupRow->addWidget(reloadGroups, 0);
    layout->addLayout(groupRow);

    layout->addWidget(new QLabel(tr("Energy terms to report (only for energy reports):")));
    m_selection = new QPlainTextEdit(this);
    m_selection->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_selection->setMaximumHeight(64);
    layout->addWidget(m_selection);

    m_plot = new XvgPlotWidget(this);
    layout->addWidget(m_plot, 1);

    auto *buttons = new QHBoxLayout;
    auto *openBtn = new QPushButton(tr("Open existing plot data…"), this);
    auto *compareBtn = new QPushButton(tr("Compare multiple runs…"), this);
    auto *exportBtn = new QPushButton(tr("Export plot (PDF, 300 dpi)"), this);
    m_runButton = new QPushButton(tr("Run analysis"), this);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    buttons->addWidget(openBtn);
    buttons->addWidget(compareBtn);
    buttons->addWidget(exportBtn);
    buttons->addStretch();
    buttons->addWidget(m_runButton);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    connect(m_preset, &QComboBox::currentIndexChanged, this, &AnalysisDialog::presetChanged);
    connect(openBtn, &QPushButton::clicked, this, &AnalysisDialog::openExistingXvg);
    connect(compareBtn, &QPushButton::clicked, this, &AnalysisDialog::compareXvgFiles);
    connect(exportBtn, &QPushButton::clicked, this, &AnalysisDialog::exportPlotPdf);
    connect(reloadGroups, &QPushButton::clicked, this, &AnalysisDialog::loadGroupChoices);
    connect(rescanBtn, &QPushButton::clicked, this, [this]() { rescanDataFiles(); });
    connect(m_groupA, &QComboBox::currentIndexChanged, this,
            [this](int) { m_selection->setPlainText(buildStdinText()); });
    connect(m_groupB, &QComboBox::currentIndexChanged, this,
            [this](int) { m_selection->setPlainText(buildStdinText()); });
    connect(m_runButton, &QPushButton::clicked, this, [this]() {
        const Preset &p = kPresets[m_preset->currentIndex()];
        const QString bin = m_gmx.isEmpty() ? QStringLiteral("gmx") : m_gmx;

        // Guard every data source the report needs — the dialog never
        // launches a report whose inputs have disappeared.
        const QString energy = m_energyFile->currentText();
        const QString runInput = m_runInput->currentText();
        const QString trajectory = m_trajectory->currentText();
        QStringList missing;
        if ((p.needs & NeedsEnergy) && !QFile::exists(QDir(m_dir).filePath(energy)))
            missing << energy;
        if ((p.needs & NeedsRunInput) && !QFile::exists(QDir(m_dir).filePath(runInput)))
            missing << runInput;
        if ((p.needs & NeedsTrajectory) && !QFile::exists(QDir(m_dir).filePath(trajectory)))
            missing << trajectory;
        if (!missing.isEmpty()) {
            QMessageBox::warning(this, tr("Data missing"),
                                 tr("The following data files are no longer in the "
                                    "project folder:\n%1\nRe-scan the folder or run "
                                    "the simulation step that produces them.")
                                     .arg(missing.join(QStringLiteral(", "))));
            return;
        }

        const QString command = QString(p.command).arg(bin, energy, runInput, trajectory);
        const QString xvg = QDir(m_dir).filePath(QString::fromUtf8(p.xvgOut));
        emit commandReady(tr("Analysis: %1").arg(m_preset->currentText()), command, m_dir,
                          buildStdinText(), xvg);
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    presetChanged(0);
    loadGroupChoices();
    rescanDataFiles();
}

void AnalysisDialog::rescanDataFiles()
{
    const QStringList edrPriority = {QStringLiteral("em.edr"), QStringLiteral("ener.edr"),
                                     QStringLiteral("nvt.edr"), QStringLiteral("npt.edr"),
                                     QStringLiteral("md.edr")};
    const QStringList tprPriority = {QStringLiteral("em.tpr"), QStringLiteral("nvt.tpr"),
                                     QStringLiteral("npt.tpr"), QStringLiteral("md.tpr")};
    const QStringList trjPriority = {QStringLiteral("md_nojump.xtc"),
                                     QStringLiteral("md.xtc"), QStringLiteral("md.trr")};

    auto fillCombo = [this](QComboBox *combo, const QStringList &priority,
                            const QStringList &patterns) {
        combo->clear();
        QStringList found;
        for (const QString &name : priority)
            if (QFile::exists(QDir(m_dir).filePath(name)))
                found << name;
        const QFileInfoList others =
            QDir(m_dir).entryInfoList(patterns, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : others) {
            if (!found.contains(fi.fileName()))
                found << fi.fileName();
        }
        combo->addItems(found);
    };
    fillCombo(m_energyFile, edrPriority, {QStringLiteral("*.edr")});
    fillCombo(m_runInput, tprPriority, {QStringLiteral("*.tpr")});
    fillCombo(m_trajectory, trjPriority, {QStringLiteral("*.xtc"), QStringLiteral("*.trr")});
    updateRunState();
}

void AnalysisDialog::updateRunState()
{
    const Preset &p = kPresets[m_preset->currentIndex()];
    QStringList missing;
    if ((p.needs & NeedsEnergy) && m_energyFile->count() == 0)
        missing << tr("energy data (*.edr)");
    if ((p.needs & NeedsRunInput) && m_runInput->count() == 0)
        missing << tr("a run input file (*.tpr)");
    if ((p.needs & NeedsTrajectory) && m_trajectory->count() == 0)
        missing << tr("a trajectory (*.xtc / *.trr)");
    const bool ready = m_core->coreAvailable() && !m_gmx.isEmpty() && missing.isEmpty();
    m_runButton->setEnabled(ready);
    if (!m_gmx.isEmpty() && !missing.isEmpty()) {
        m_dataStatus->setText(tr("This report needs %1 — run the corresponding "
                                 "simulation step first.")
                                  .arg(missing.join(tr(" and "))));
        m_dataStatus->setStyleSheet(QStringLiteral("color:#a06000;"));
    } else {
        m_dataStatus->setText(tr("Found: %1 energy files · %2 run inputs · %3 trajectories")
                                  .arg(m_energyFile->count())
                                  .arg(m_runInput->count())
                                  .arg(m_trajectory->count()));
        m_dataStatus->setStyleSheet(QStringLiteral("color:#2d6b2d;"));
    }
}

void AnalysisDialog::loadGroupChoices()
{
    const QString structure = QDir(m_dir).filePath(QStringLiteral("protein.gro"));
    m_groupA->clear();
    m_groupB->clear();
    if (!QFile::exists(structure))
        return;
    const QVector<IndexGroupInfo> groups = m_core->indexGroups(structure);
    int idx = 0;
    for (const IndexGroupInfo &g : groups) {
        const QString entry = QStringLiteral("%1 (%2 atoms)").arg(g.name).arg(g.count);
        m_groupA->addItem(entry, idx);
        m_groupB->addItem(entry, idx);
        ++idx;
    }
}

QString AnalysisDialog::buildStdinText() const
{
    // Energy reports carry their explicit term-name selection; trajectory
    // reports get the group numbers chosen in the dialog ("group2 group1\n"
    // mirrors the terminal flow: the reference group is asked first, then
    // the calculation group).
    const Preset &p = kPresets[m_preset->currentIndex()];
    const QString terms = QString::fromUtf8(p.stdinText);
    if (!terms.isEmpty())
        return terms;

    const int a = m_groupA->currentData().isValid() ? m_groupA->currentData().toInt() : 0;
    const int b = m_groupB->currentData().isValid() ? m_groupB->currentData().toInt() : a;
    return p.groupPrompts >= 2 ? QStringLiteral("%1\n%2\n").arg(a).arg(b)
                               : QStringLiteral("%1\n").arg(a);
}

void AnalysisDialog::presetChanged(int index)
{
    if (index < 0)
        return;
    // Build the selection text and load the result file directly when it
    // already exists from an earlier run.
    m_selection->setPlainText(buildStdinText());
    const Preset &p = kPresets[index];
    const QString xvg = QDir(m_dir).filePath(QString::fromUtf8(p.xvgOut));
    if (QFile::exists(xvg))
        jobCompleted(xvg);
    updateRunState();
}

void AnalysisDialog::openExistingXvg()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open xvg file"), m_dir,
                                                      tr("Plot data (*.xvg *.dat)"));
    if (!path.isEmpty())
        jobCompleted(path);
}

void AnalysisDialog::jobCompleted(const QString &xvgHint)
{
    QString path = xvgHint;
    if (path.isEmpty() || !QFile::exists(path)) {
        // pick the newest .xvg in the project directory
        const QFileInfoList xvgs =
            QDir(m_dir).entryInfoList({QStringLiteral("*.xvg")}, QDir::Files,
                                      QDir::Time | QDir::Reversed);
        if (xvgs.isEmpty())
            return;
        path = xvgs.first().absoluteFilePath();
    }
    const XvgData data = m_core->parseXvg(path);
    m_plot->setXvgData(data);
}

void AnalysisDialog::compareXvgFiles()
{
    // F7 — multi-run comparison: several xvg files (e.g. RMSD from different
    // MDP settings) are overlaid in one plot, one colour per file.
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Select .xvg files to compare"), m_dir, tr("Plot data (*.xvg)"));
    if (files.isEmpty())
        return;

    XvgData merged;
    merged.xlabel = tr("Time (ps)");
    merged.ylabel = tr("Value");
    for (const QString &file : files) {
        XvgData d = m_core->parseXvg(file);
        const QString label = QFileInfo(file).fileName();
        if (d.series.isEmpty()) {
            XvgSeries empty;
            empty.legend = label;
            merged.series.push_back(empty);
            continue;
        }
        for (const XvgSeries &s : d.series) {
            XvgSeries copy = s;
            copy.legend = d.series.size() > 1
                ? QStringLiteral("%1 — %2").arg(label, s.legend)
                : label;
            merged.series.push_back(copy);
        }
    }
    m_plot->setXvgData(merged);
}

void AnalysisDialog::exportPlotPdf()
{
    // F8 — publication-quality export: the plot is re-rendered into a PDF
    // canvas through QPdfWriter at 300 dpi.
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export plot as PDF"), QDir(m_dir).filePath(QStringLiteral("plot.pdf")),
        tr("PDF (*.pdf)"));
    if (path.isEmpty())
        return;

    QPdfWriter writer(path);
    writer.setResolution(300);
    writer.setPageMargins(QMarginsF(10, 10, 10, 10));
    QPainter painter(&writer);
    // Scale the widget-sized plot onto the (smaller) page rect.
    const QRectF page(0, 0, writer.width(), writer.height());
    const QRectF source(0, 0, m_plot->width(), m_plot->height());
    const double scale = qMin(page.width() / source.width(),
                              page.height() / source.height());
    painter.scale(scale, scale);
    m_plot->render(&painter);
    painter.end();
    if (QFile::exists(path))
        m_plot->setToolTip(tr("Exported: %1").arg(path));
}
