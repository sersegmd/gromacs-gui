#include "MdpEditorPanel.h"

#include "core/CoreBridge.h"
#include "gui/MdpTemplates.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

struct TemplateEntry {
    const char *label;
    QString (*factory)();
};

const TemplateEntry kTemplates[] = {
    {"em — Energy Minimisation", &MdpTemplates::minim},
    {"ions — Ion Setup",         &MdpTemplates::ions},
    {"nvt — NVT Equilibration",  &MdpTemplates::nvt},
    {"npt — NPT Equilibration",  &MdpTemplates::npt},
    {"md — Production MD",       &MdpTemplates::production},
};

} // namespace

MdpEditorPanel::MdpEditorPanel(QWidget *parent)
    : QDockWidget(tr("MDP Editor"), parent)
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *topRow = new QHBoxLayout;
    m_templateBox = new QComboBox(container);
    for (const TemplateEntry &t : kTemplates)
        m_templateBox->addItem(QString::fromUtf8(t.label));
    auto *newButton = new QPushButton(tr("New"), container);
    auto *saveButton = new QPushButton(tr("Save"), container);
    auto *validateButton = new QPushButton(tr("Check"), container);
    validateButton->setToolTip(tr("Check the parameter set for common mistakes"));

    topRow->addWidget(m_templateBox, 1);
    topRow->addWidget(newButton);
    topRow->addWidget(saveButton);
    topRow->addWidget(validateButton);

    m_editor = new QPlainTextEdit(container);
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setPlaceholderText(
        tr("Open an .mdp file from the project tree, or create one from a template"));

    m_status = new QLabel(container);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral(
        "QLabel { color: #666; font-size: 11px; }"));

    layout->addLayout(topRow);
    layout->addWidget(m_editor, 1);
    layout->addWidget(m_status);
    setWidget(container);

    connect(newButton, &QPushButton::clicked, this, &MdpEditorPanel::newFromTemplate);
    connect(saveButton, &QPushButton::clicked, this, &MdpEditorPanel::saveCurrent);
    connect(validateButton, &QPushButton::clicked, this, &MdpEditorPanel::validateNow);
    connect(m_editor, &QPlainTextEdit::textChanged, this,
            [this]() { m_status->clear(); });
}

void MdpEditorPanel::openFile(const QString &path)
{
    if (!path.endsWith(QLatin1String(".mdp"), Qt::CaseInsensitive))
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QTextStream in(&file);
    m_editor->setPlainText(in.readAll());
    m_currentPath = path;
    setWindowTitle(tr("MDP Editor — %1").arg(QFileInfo(path).fileName()));
    m_status->clear();
}

void MdpEditorPanel::newFromTemplate()
{
    const int index = m_templateBox->currentIndex();
    m_editor->setPlainText(kTemplates[index].factory());
    m_currentPath.clear();
    setWindowTitle(tr("MDP Editor — %1 (not saved)")
                       .arg(m_templateBox->currentText().section(' ', 0, 0)));
    m_status->clear();
}

void MdpEditorPanel::saveCurrent()
{
    QString path = m_currentPath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, tr("Save MDP File"), QString(),
            tr("Simulation parameter files (*.mdp)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(QLatin1String(".mdp"), Qt::CaseInsensitive))
            path += QLatin1String(".mdp");
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save Failed"),
                             tr("Cannot write %1: %2").arg(path, file.errorString()));
        return;
    }
    QTextStream out(&file);
    out << m_editor->toPlainText();
    m_currentPath = path;
    setWindowTitle(tr("MDP Editor — %1").arg(QFileInfo(path).fileName()));
    emit fileSaved(path);
}

void MdpEditorPanel::validateNow()
{
    updateValidation();
}

void MdpEditorPanel::updateValidation()
{
    if (!m_core) {
        m_status->setText(tr("Core not available — validation disabled."));
        return;
    }
    const QVector<MdpIssue> issues = m_core->validateMdp(m_editor->toPlainText());
    if (issues.isEmpty()) {
        m_status->setStyleSheet(QStringLiteral(
            "QLabel { color: #2d862d; font-size: 11px; font-weight: bold; }"));
        m_status->setText(tr("✔ No issues found — grompp-ready."));
        return;
    }
    int errors = 0, warnings = 0;
    QString html;
    for (const MdpIssue &issue : issues) {
        if (issue.severity == "error") ++errors;
        else if (issue.severity == "warning") ++warnings;
        const QString color = issue.severity == "error" ? "#a82c2c"
                            : issue.severity == "warning" ? "#b07d1c" : "#1c6bb0";
        html += QStringLiteral("<span style=\"color:%1;\"><b>%2</b> [%3] %4</span><br>")
                    .arg(color,
                         issue.severity.toUpper(),
                         issue.key,
                         issue.message.toHtmlEscaped());
    }
    m_status->setStyleSheet(QStringLiteral("QLabel { font-size: 11px; }"));
    m_status->setText(tr("%1 error(s), %2 warning(s):<br>%3")
                          .arg(errors).arg(warnings).arg(html));
}
