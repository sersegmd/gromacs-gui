#include "ResourceEstimatorDialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace {
// Heuristic calibration for desktop-class hardware (OpenMP CPU runs):
// RAM grows linearly with system size, performance scales roughly
// sub-linearly with thread count. GPU gives an additional ~3x boost for
// the non-bonded work that dominates biomolecular MD.
constexpr double kRamBaseGB = 0.30;      // binary + topology + overhead
constexpr double kRamGBPerMillionAtoms = 4.8;
constexpr double kNsDayCoresRef = 4.65e5; // calibrated: 8 threads, 40k atoms → ~50 ns/day
constexpr double kGpuSpeedup = 3.0;
constexpr double kThreadExponent = 0.7;
} // namespace

ResourceEstimatorDialog::ResourceEstimatorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Resource Estimator (heuristic)"));
    resize(520, 380);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_atoms = new QLineEdit(QStringLiteral("40000"), this);
    form->addRow(tr("System size (atoms):"), m_atoms);
    m_ns = new QLineEdit(QStringLiteral("50"), this);
    form->addRow(tr("Simulation length (ns):"), m_ns);
    m_dt = new QLineEdit(QStringLiteral("0.002"), this);
    form->addRow(tr("Integration time step dt (ps):"), m_dt);
    m_threads = new QComboBox(this);
    m_threads->addItems({QStringLiteral("2"), QStringLiteral("4"), QStringLiteral("8"),
                         QStringLiteral("16"), QStringLiteral("32")});
    m_threads->setCurrentIndex(2);
    form->addRow(tr("CPU threads (mdrun -ntomp):"), m_threads);
    m_gpu = new QComboBox(this);
    m_gpu->addItems({tr("CPU only"), tr("One GPU (-nb gpu)"), tr("Two GPUs")});
    form->addRow(tr("GPU acceleration:"), m_gpu);
    layout->addLayout(form);

    m_ram = new QLabel(this);
    m_perf = new QLabel(this);
    m_wall = new QLabel(this);
    m_note = new QLabel(this);
    QFont big = m_ram->font();
    big.setPointSize(big.pointSize() + 1);
    big.setBold(true);
    for (QLabel *l : {m_ram, m_perf, m_wall}) {
        l->setFont(big);
        l->setAlignment(Qt::AlignCenter);
        layout->addWidget(l);
    }
    layout->addSpacing(8);
    m_note->setWordWrap(true);
    m_note->setStyleSheet(QStringLiteral("color:#666;"));
    layout->addWidget(m_note);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    layout->addWidget(closeBtn);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_atoms, &QLineEdit::textChanged, this, &ResourceEstimatorDialog::recompute);
    connect(m_ns, &QLineEdit::textChanged, this, &ResourceEstimatorDialog::recompute);
    connect(m_dt, &QLineEdit::textChanged, this, &ResourceEstimatorDialog::recompute);
    connect(m_threads, &QComboBox::currentIndexChanged, this, &ResourceEstimatorDialog::recompute);
    connect(m_gpu, &QComboBox::currentIndexChanged, this, &ResourceEstimatorDialog::recompute);

    recompute();
}

void ResourceEstimatorDialog::recompute()
{
    const double atoms = m_atoms->text().toDouble();
    const double ns = m_ns->text().toDouble();
    const double dt = m_dt->text().toDouble();
    if (atoms <= 0 || ns <= 0 || dt <= 0) {
        m_ram->setText(tr("Enter positive numbers."));
        m_perf->clear();
        m_wall->clear();
        return;
    }

    // RAM: base + per-atom cost (coordinates, topology arrays, PME grids).
    const double ramGB = kRamBaseGB + kRamGBPerMillionAtoms * atoms / 1.0e6;
    m_ram->setText(tr("Estimated peak RAM: %1 GB").arg(QString::number(ramGB, 'f', 1)));

    // Performance: sub-linear thread scaling + GPU boost.
    const int threads = m_threads->currentText().toInt();
    const double gpuFactor = m_gpu->currentIndex() == 0
        ? 1.0
        : (m_gpu->currentIndex() == 1 ? kGpuSpeedup : kGpuSpeedup * 1.8);
    const double nsPerDay = kNsDayCoresRef * std::pow(threads, kThreadExponent) * gpuFactor / atoms;
    m_perf->setText(tr("Estimated performance: %1 ns/day").arg(QString::number(nsPerDay, 'f', 1)));

    // Wall-clock: steps = ns/dt; time = steps / nsPerDay.
    const double days = ns / qMax(nsPerDay, 1e-9);
    const double hours = days * 24.0;
    if (hours < 48.0)
        m_wall->setText(tr("Expected wall time: %1 h").arg(QString::number(hours, 'f', 1)));
    else
        m_wall->setText(tr("Expected wall time: %1 days").arg(QString::number(days, 'f', 1)));

    m_note->setText(tr("Rough desktop-hardware heuristics (OpenMP, PME, TIP3P water).\n"
                       "Real numbers depend on CPU model, GPU generation, cutoffs and system\n"
                       "composition — always benchmark with a short mdrun -maxh 0.1 test first.\n"
                       "Tip: use mdrun -cpi to checkpoint so long runs can be resumed."));
}
