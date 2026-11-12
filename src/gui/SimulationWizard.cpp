#include "SimulationWizard.h"

#include "core/CoreBridge.h"
#include "core/JobManager.h"
#include "gui/MdpTemplates.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

SimulationWizard::SimulationWizard(CoreBridge *core, JobManager *jobs, const QString &projectDir,
                                   const QString &gmxPath, QWidget *parent)
    : QDialog(parent)
    , m_core(core)
    , m_jobs(jobs)
    , m_dir(projectDir.isEmpty() ? QStringLiteral(".") : projectDir)
    , m_gmx(gmxPath)
{
    setWindowTitle(tr("Simulation Setup Wizard"));
    resize(940, 600);

    auto *layout = new QVBoxLayout(this);

    // -- system template selector (F2) --------------------------------------
    auto *templateRow = new QHBoxLayout;
    templateRow->addWidget(new QLabel(tr("System template:")));
    m_template = new QComboBox(this);
    m_template->addItems({
        tr("Protein in water"),
        tr("Protein + ligand"),
        tr("Membrane (protein into bilayer)"),
        tr("Polymer in water"),
    });
    templateRow->addWidget(m_template, 1);
    layout->addLayout(templateRow);

    auto *body = new QHBoxLayout;

    m_stepList = new QListWidget(this);
    m_stepList->setFixedWidth(250);
    body->addWidget(m_stepList);

    m_pages = new QStackedWidget(this);
    body->addWidget(m_pages, 1);
    layout->addLayout(body, 1);

    // ---- page: pdb2gmx ----------------------------------------------------
    auto *p0 = new QWidget(this);
    auto *f0 = new QFormLayout(p0);
    m_pdbInput = new QLineEdit(QStringLiteral("1lyz.pdb"), p0);
    auto *browse0 = new QPushButton(tr("Browse…"), p0);
    auto *row0 = new QHBoxLayout;
    row0->addWidget(m_pdbInput, 1);
    row0->addWidget(browse0);
    f0->addRow(tr("Input structure:"), row0);
    m_forceField = new QComboBox(p0);
    m_forceField->addItems({QStringLiteral("oplsaa"), QStringLiteral("amber99sb-ildn"),
                            QStringLiteral("amber14sb"), QStringLiteral("charmm27"),
                            QStringLiteral("gromos54a7")});
    f0->addRow(tr("Force field:"), m_forceField);
    m_waterModel = new QComboBox(p0);
    m_waterModel->addItems({QStringLiteral("tip3p"), QStringLiteral("spce"),
                            QStringLiteral("spc"), QStringLiteral("tip4p")});
    f0->addRow(tr("Water model:"), m_waterModel);
    f0->addRow(new QLabel(tr("Generates protein.gro / protein.top in the project directory.\n"
                             "(The Polymer template skips this step — polymers usually ship\n"
                             "with a pre-built topology.)")));
    m_pages->addWidget(p0);

    // ---- page: ligand (protein + ligand template) --------------------------
    auto *p1 = new QWidget(this);
    auto *f1 = new QFormLayout(p1);
    m_ligandInput = new QLineEdit(QStringLiteral("ligand.pdb"), p1);
    auto *browseLig = new QPushButton(tr("Browse…"), p1);
    auto *rowLig = new QHBoxLayout;
    rowLig->addWidget(m_ligandInput, 1);
    rowLig->addWidget(browseLig);
    f1->addRow(tr("Ligand structure:"), rowLig);
    f1->addRow(new QLabel(tr("The ligand is reboxed and merged with the protein\n"
                             "structure. IMPORTANT: ligand force-field parameters must be\n"
                             "generated externally (CGenFF, Acpype/ANTECHAMBER, OpenFF) and\n"
                             "included in protein.top before continuing — the simulation\n"
                             "engine itself does not parameterise small molecules.")));
    m_pages->addWidget(p1);

    // ---- page: membrane insertion -----------------------------------------
    auto *p2 = new QWidget(this);
    auto *f2 = new QFormLayout(p2);
    m_membraneInput = new QLineEdit(QStringLiteral("bilayer.gro"), p2);
    auto *browseMem = new QPushButton(tr("Browse…"), p2);
    auto *rowMem = new QHBoxLayout;
    rowMem->addWidget(m_membraneInput, 1);
    rowMem->addWidget(browseMem);
    f2->addRow(tr("Pre-equilibrated bilayer:"), rowMem);
    f2->addRow(new QLabel(tr("Embeds the boxed protein into the bilayer coordinate\n"
                             "file. For production-grade membranes build the bilayer\n"
                             "with CHARMM-GUI; Molecular Studio then drives the standard\n"
                             "chain on top of it.")));
    m_pages->addWidget(p2);

    // ---- page: editconf ---------------------------------------------------
    auto *p3 = new QWidget(this);
    auto *f3 = new QFormLayout(p3);
    m_boxType = new QComboBox(p3);
    m_boxType->addItems({QStringLiteral("dodecahedron"), QStringLiteral("cubic"),
                         QStringLiteral("triclinic")});
    f3->addRow(tr("Box type:"), m_boxType);
    m_boxDistance = new QLineEdit(QStringLiteral("1.0"), p3);
    f3->addRow(tr("Distance to solute (nm):"), m_boxDistance);
    m_polymerInput = new QLineEdit(QStringLiteral("polymer.gro"), p3);
    auto *browsePoly = new QPushButton(tr("Browse…"), p3);
    auto *rowPoly = new QHBoxLayout;
    rowPoly->addWidget(m_polymerInput, 1);
    rowPoly->addWidget(browsePoly);
    f3->addRow(tr("Polymer input (Polymer template):"), rowPoly);
    f3->addRow(new QLabel(tr("Protein template: protein.gro → boxed.gro, centred in the box.\n"
                             "Membrane template: use distance ≥ 2.5 nm for solvent above/below\n"
                             "the bilayer. Polymer template: polymer.gro → boxed.gro.")));
    m_pages->addWidget(p3);

    // ---- page: solvate ----------------------------------------------------
    auto *p4 = new QWidget(this);
    auto *f4 = new QFormLayout(p4);
    f4->addRow(new QLabel(tr("Fills the box with water (the standard pre-equilibrated\n"
                             "water box shipped with the engine). protein.top gains\n"
                             "the SOL molecule entries.")));
    m_pages->addWidget(p4);

    // ---- page: ions -------------------------------------------------------
    auto *p5 = new QWidget(this);
    auto *f5 = new QFormLayout(p5);
    m_pName = new QLineEdit(QStringLiteral("NA"), p5);
    f5->addRow(tr("Positive ion:"), m_pName);
    m_nName = new QLineEdit(QStringLiteral("CL"), p5);
    f5->addRow(tr("Negative ion:"), m_nName);
    f5->addRow(new QLabel(tr("Compiles the solvated system, then replaces water molecules\n"
                             "with ions to neutralise the charge. Output: solvated_ions.gro.")));
    m_pages->addWidget(p5);

    // ---- pages: minim / NVT / NPT ------------------------------------------
    const char *descriptions[] = {
        "Writes minim.mdp (steep descent) and runs the minimisation. Outputs em.gro / em.tpr / em.edr.",
        "Writes nvt.mdp (V-rescale 300 K) and runs 100 ps at constant volume. Outputs nvt.*.",
        "Writes npt.mdp (C-rescale 1 bar) and runs 100 ps at constant pressure. Outputs npt.*.",
    };
    for (const char *d : descriptions) {
        auto *page = new QWidget(this);
        auto *form = new QFormLayout(page);
        form->addRow(new QLabel(tr(d)));
        m_pages->addWidget(page);
    }

    // ---- page: production MD ----------------------------------------------
    auto *pMd = new QWidget(this);
    auto *fMd = new QFormLayout(pMd);
    fMd->addRow(new QLabel(tr("Writes md.mdp (production settings) and runs the final\n"
                              "unrestrained simulation. Outputs md.gro, md.xtc and md.edr —\n"
                              "the data every analysis view uses.")));
    m_resumeCheckpoint = new QCheckBox(tr("Resume an interrupted run from the saved checkpoint"), pMd);
    fMd->addRow(m_resumeCheckpoint);
    fMd->addRow(new QLabel(tr("Checkpointing lets a killed run continue exactly where it\n"
                              "stopped instead of starting over.")));
    m_pages->addWidget(pMd);

    // ---- preview + buttons ------------------------------------------------
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_preview->setMaximumHeight(120);
    layout->addWidget(m_preview);

    auto *buttons = new QHBoxLayout;
    m_runStepBtn = new QPushButton(tr("Run this step"), this);
    m_runAllBtn = new QPushButton(tr("Run full pipeline"), this);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    buttons->addWidget(m_runStepBtn);
    buttons->addWidget(m_runAllBtn);
    buttons->addStretch();
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);
    auto *seqNote = new QLabel(
        tr("Pipeline steps run strictly one after another; a step starts only "
           "when its required files exist, and the chain stops at the first "
           "failure so you can fix it before continuing."), this);
    seqNote->setWordWrap(true);
    layout->addWidget(seqNote);

    connect(m_template, &QComboBox::currentIndexChanged, this, &SimulationWizard::templateChanged);
    connect(m_stepList, &QListWidget::currentRowChanged, m_pages,
            &QStackedWidget::setCurrentIndex);
    connect(m_stepList, &QListWidget::currentRowChanged, this,
            [this](int row) {
                if (row >= 0)
                    updatePreview();
            });
    connect(browse0, &QPushButton::clicked, this,
            [this]() { browseInput(m_pdbInput, tr("Input structure")); });
    connect(browseLig, &QPushButton::clicked, this,
            [this]() { browseInput(m_ligandInput, tr("Ligand structure")); });
    connect(browseMem, &QPushButton::clicked, this,
            [this]() { browseInput(m_membraneInput, tr("Bilayer coordinates")); });
    connect(browsePoly, &QPushButton::clicked, this,
            [this]() { browseInput(m_polymerInput, tr("Polymer coordinates")); });
    connect(m_runStepBtn, &QPushButton::clicked, this, &SimulationWizard::runCurrentStep);
    connect(m_runAllBtn, &QPushButton::clicked, this, &SimulationWizard::runFullPipeline);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    const bool ready = m_core->coreAvailable() && !m_gmx.isEmpty();
    m_runStepBtn->setEnabled(ready);
    m_runAllBtn->setEnabled(ready);
    templateChanged(0);
}

QString SimulationWizard::browseInput(QLineEdit *field, const QString &title)
{
    const QString path = QFileDialog::getOpenFileName(this, title, m_dir,
                                                      tr("Structure files (*.pdb *.gro)"));
    if (!path.isEmpty()) {
        field->setText(path);
        updatePreview();
    }
    return path;
}

QVector<SimulationWizard::StepId> SimulationWizard::stepsForTemplate(int templateIndex) const
{
    // Base chain shared by every template.
    QVector<StepId> steps = {StepBox, StepSolvate, StepIons, StepMinim, StepNvt, StepNpt, StepMd};
    switch (templateIndex) {
    case 0:   // Protein in water
        steps.prepend(StepPdb2gmx);
        break;
    case 1:   // Protein + ligand
        steps.prepend(StepLigand);
        steps.prepend(StepPdb2gmx);
        break;
    case 2:   // Membrane
        steps.prepend(StepMembraneInsert);
        steps.prepend(StepPdb2gmx);
        break;
    case 3:   // Polymer (no pdb2gmx)
    default:
        break;
    }
    return steps;
}

void SimulationWizard::templateChanged(int index)
{
    Q_UNUSED(index);
    rebuildStepList();
    updatePreview();
}

void SimulationWizard::rebuildStepList()
{
    m_stepList->blockSignals(true);
    m_stepList->clear();
    const QVector<StepId> steps = stepsForTemplate(m_template->currentIndex());
    static const QStringList names = {
        tr("1. Topology generation"),
        tr("1b. Ligand prepare + merge"),
        tr("1c. Insert into bilayer"),
        tr("2. Define the box"),
        tr("3. Solvate"),
        tr("4. Ion neutralisation"),
        tr("5. Energy minimisation"),
        tr("6. NVT equilibration"),
        tr("7. NPT equilibration"),
        tr("8. Production MD"),
    };
    for (StepId id : steps) {
        m_stepList->addItem(names.value(int(id)));
        // Remember which page each list row drives.
        m_stepList->item(m_stepList->count() - 1)->setData(Qt::UserRole, int(id));
    }
    m_stepList->blockSignals(false);
    if (m_stepList->count() > 0)
        m_stepList->setCurrentRow(0);
}

QVector<WorkflowStep> SimulationWizard::stepsForStepId(StepId id) const
{
    const QString bin = m_gmx.isEmpty() ? QStringLiteral("gmx") : m_gmx;
    QVector<WorkflowStep> steps;
    const bool isLigand = m_template->currentIndex() == 1;
    const bool isMembrane = m_template->currentIndex() == 2;
    const bool isPolymer = m_template->currentIndex() == 3;
    const QString pdbInput = QFileInfo(m_pdbInput->text()).fileName();
    const QString ligandInput = QFileInfo(m_ligandInput->text()).fileName();
    const QString polymerInput = QFileInfo(m_polymerInput->text()).fileName();

    switch (id) {
    case StepPdb2gmx:
        steps.push_back({tr("Topology generation"),
            QStringLiteral("%1 pdb2gmx -f %2 -o protein.gro -p protein.top -ff %3 -water %4 -ignh")
                .arg(bin, pdbInput, m_forceField->currentText(), m_waterModel->currentText()),
            m_dir,
            {pdbInput},
            QString(),
            tr("Reads the input structure, adds hydrogens and builds the "
               "force-field topology (protein.gro + protein.top).")});
        break;
    case StepLigand:
        steps.push_back({tr("Ligand preparation"),
            QStringLiteral("%1 editconf -f %2 -o ligand.gro -resnm LIG")
                .arg(bin, ligandInput),
            m_dir,
            {ligandInput},
            QString(),
            tr("Reboxes the ligand coordinates ready for merging.")});
        steps.push_back({tr("Merge ligand with protein"),
            QStringLiteral("grep -v 'END' protein.gro > merged.gro && cat ligand.gro >> merged.gro && echo END >> merged.gro"),
            m_dir,
            {QStringLiteral("protein.gro"), QStringLiteral("ligand.gro")},
            QString(),
            tr("Appends the ligand to the protein structure. Ligand force-field "
               "parameters must be prepared externally and included in "
               "protein.top before continuing.")});
        break;
    case StepMembraneInsert:
        steps.push_back({tr("Insert protein into bilayer"),
            QStringLiteral("%1 insert-molecules -f boxed.gro -ci %2 -o mem_prot.gro")
                .arg(bin, QFileInfo(m_membraneInput->text()).fileName()),
            m_dir,
            {QStringLiteral("boxed.gro"), QFileInfo(m_membraneInput->text()).fileName()},
            QString(),
            tr("Embeds the boxed protein into the pre-equilibrated bilayer.")});
        break;
    case StepBox:
        if (isPolymer)
            steps.push_back({tr("Define the simulation box"),
                QStringLiteral("%1 editconf -f %2 -o boxed.gro -bt %3 -d %4 -c")
                    .arg(bin, polymerInput, m_boxType->currentText(), m_boxDistance->text()),
                m_dir,
                {polymerInput},
                QString(),
                tr("Centres the structure in a %1 box with at least %2 nm of "
                   "solvent on all sides.").arg(m_boxType->currentText(), m_boxDistance->text())});
        else if (isLigand)
            steps.push_back({tr("Define the simulation box"),
                QStringLiteral("%1 editconf -f merged.gro -o boxed.gro -bt %2 -d %3 -c")
                    .arg(bin, m_boxType->currentText(), m_boxDistance->text()),
                m_dir,
                {QStringLiteral("merged.gro")},
                QString(),
                tr("Centres the complex in a %1 box with at least %2 nm of "
                   "solvent on all sides.").arg(m_boxType->currentText(), m_boxDistance->text())});
        else if (isMembrane)
            steps.push_back({tr("Define the simulation box"),
                QStringLiteral("%1 editconf -f protein.gro -o boxed.gro -bt %2 -d %3 -c")
                    .arg(bin, m_boxType->currentText(), m_boxDistance->text()),
                m_dir,
                {QStringLiteral("protein.gro")},
                QString(),
                tr("Centres the protein before bilayer insertion (use ≥ 2.5 nm "
                   "for solvent above and below the membrane).")});
        else
            steps.push_back({tr("Define the simulation box"),
                QStringLiteral("%1 editconf -f protein.gro -o boxed.gro -bt %2 -d %3 -c")
                    .arg(bin, m_boxType->currentText(), m_boxDistance->text()),
                m_dir,
                {QStringLiteral("protein.gro")},
                QString(),
                tr("Centres the protein in a %1 box with at least %2 nm of "
                   "solvent on all sides.").arg(m_boxType->currentText(), m_boxDistance->text())});
        break;
    case StepSolvate: {
        const QString input = isMembrane ? QStringLiteral("mem_prot.gro")
                                         : QStringLiteral("boxed.gro");
        steps.push_back({tr("Solvate the box"),
            QStringLiteral("%1 solvate -cp %2 -cs spc216.gro -o solvated.gro -p protein.top")
                .arg(bin, input),
            m_dir,
            {input, QStringLiteral("protein.top")},
            QString(),
            tr("Fills the box with water and records the solvent count in the "
               "topology.")});
        break;
    }
    case StepIons:
        steps.push_back({tr("Prepare ion replacement"),
            QStringLiteral("%1 grompp -f ions.mdp -c solvated.gro -p protein.top -o ions.tpr -maxwarn 1")
                .arg(bin),
            m_dir,
            {QStringLiteral("solvated.gro"), QStringLiteral("ions.mdp"),
             QStringLiteral("protein.top")},
            QString(),
            tr("Compiles the solvated system for the neutralising step.")});
        steps.push_back({tr("Add ions"),
            QStringLiteral("%1 genion -s ions.tpr -o solvated_ions.gro -p protein.top -pname %2 -nname %3 -neutral")
                .arg(bin, m_pName->text(), m_nName->text()),
            m_dir,
            {QStringLiteral("ions.tpr")},
            QStringLiteral("SOL\n"),
            tr("Replaces water molecules with Na+/Cl- so the system carries "
               "no net charge.")});
        break;
    case StepMinim:
        steps.push_back({tr("Energy minimisation — set up"),
            QStringLiteral("%1 grompp -f minim.mdp -c solvated_ions.gro -p protein.top -o em.tpr -maxwarn 1")
                .arg(bin),
            m_dir,
            {QStringLiteral("solvated_ions.gro"), QStringLiteral("minim.mdp"),
             QStringLiteral("protein.top")},
            QString(),
            tr("Compiles the minimised system parameters.")});
        steps.push_back({tr("Energy minimisation — run"),
            QStringLiteral("%1 mdrun -deffnm em").arg(bin),
            m_dir,
            {QStringLiteral("em.tpr")},
            QString(),
            tr("Relaxes steric clashes until the maximum force drops below "
               "the threshold. Produces em.gro / em.edr.")});
        break;
    case StepNvt:
        steps.push_back({tr("NVT equilibration — set up"),
            QStringLiteral("%1 grompp -f nvt.mdp -c em.gro -r em.gro -p protein.top -o nvt.tpr -maxwarn 1")
                .arg(bin),
            m_dir,
            {QStringLiteral("em.gro"), QStringLiteral("nvt.mdp"), QStringLiteral("protein.top")},
            QString(),
            tr("Heats the system to 300 K at constant volume, with position "
               "restraints on the solute.")});
        steps.push_back({tr("NVT equilibration — run"),
            QStringLiteral("%1 mdrun -deffnm nvt").arg(bin),
            m_dir,
            {QStringLiteral("nvt.tpr")},
            QString(),
            tr("100 ps NVT run. Produces nvt.gro / nvt.cpt.")});
        break;
    case StepNpt:
        steps.push_back({tr("NPT equilibration — set up"),
            QStringLiteral("%1 grompp -f npt.mdp -c nvt.gro -r nvt.gro -t nvt.cpt -p protein.top -o npt.tpr -maxwarn 1")
                .arg(bin),
            m_dir,
            {QStringLiteral("nvt.gro"), QStringLiteral("nvt.cpt"), QStringLiteral("npt.mdp"),
             QStringLiteral("protein.top")},
            QString(),
            tr("Pressurises the system to 1 bar, continuing from the NVT "
               "checkpoint.")});
        steps.push_back({tr("NPT equilibration — run"),
            QStringLiteral("%1 mdrun -deffnm npt").arg(bin),
            m_dir,
            {QStringLiteral("npt.tpr")},
            QString(),
            tr("100 ps NPT run. Produces npt.gro / npt.cpt.")});
        break;
    case StepMd:
        steps.push_back({tr("Production MD — set up"),
            QStringLiteral("%1 grompp -f md.mdp -c npt.gro -r npt.gro -t npt.cpt -p protein.top -o md.tpr -maxwarn 1")
                .arg(bin),
            m_dir,
            {QStringLiteral("npt.gro"), QStringLiteral("npt.cpt"), QStringLiteral("md.mdp"),
             QStringLiteral("protein.top")},
            QString(),
            tr("Compiles the production run parameters.")});
        steps.push_back({m_resumeCheckpoint && m_resumeCheckpoint->isChecked()
                ? tr("Production MD — run (resume from checkpoint)")
                : tr("Production MD — run"),
            m_resumeCheckpoint && m_resumeCheckpoint->isChecked()
                ? QStringLiteral("%1 mdrun -deffnm md -cpi md.cpt -noappend").arg(bin)
                : QStringLiteral("%1 mdrun -deffnm md").arg(bin),
            m_dir,
            {QStringLiteral("md.tpr")},
            QString(),
            tr("The final unrestrained simulation. Produces md.gro, md.xtc "
               "and md.edr — the data used by every analysis view.")});
        break;
    default:
        break;
    }
    return steps;
}

QVector<WorkflowStep> SimulationWizard::stepsForPage(int page) const
{
    // The stacked pages are ordered exactly like the StepId enum.
    if (page < 0 || page >= StepCount)
        return {};
    return stepsForStepId(static_cast<StepId>(page));
}

void SimulationWizard::writeMdpForStep(StepId id)
{
    const QHash<int, QPair<QString, QString>> mdpForStep = {
        {StepIons,   {QStringLiteral("ions.mdp"),  MdpTemplates::ions()}},
        {StepMinim,  {QStringLiteral("minim.mdp"), MdpTemplates::minim()}},
        {StepNvt,    {QStringLiteral("nvt.mdp"),   MdpTemplates::nvt()}},
        {StepNpt,    {QStringLiteral("npt.mdp"),   MdpTemplates::npt()}},
        {StepMd,     {QStringLiteral("md.mdp"),    MdpTemplates::production()}},
    };
    if (mdpForStep.contains(int(id))) {
        const auto &entry = mdpForStep.value(int(id));
        QFile file(QDir(m_dir).filePath(entry.first));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
            file.write(entry.second.toUtf8());
    }
}

void SimulationWizard::updatePreview()
{
    // Friendly summary of the selected step: what it does and what it
    // produces — implementation details stay out of the user's face.
    QListWidgetItem *item = m_stepList->currentItem();
    if (!item) {
        m_preview->clear();
        return;
    }
    const StepId id = static_cast<StepId>(item->data(Qt::UserRole).toInt());
    writeMdpForStep(id);
    QString text;
    const QVector<WorkflowStep> steps = stepsForStepId(id);
    for (int i = 0; i < steps.size(); ++i) {
        if (i > 0)
            text += QStringLiteral("\n");
        text += tr("▸ %1\n    %2").arg(steps.at(i).label, steps.at(i).description);
    }
    text += QStringLiteral("\n\n") + tr("Required inputs are verified before the step "
                                        "starts; the chain stops at the first failure.");
    m_preview->setPlainText(text);
}

void SimulationWizard::runCurrentStep()
{
    QListWidgetItem *item = m_stepList->currentItem();
    if (!item)
        return;
    const StepId id = static_cast<StepId>(item->data(Qt::UserRole).toInt());
    writeMdpForStep(id);
    emit pipelineReady(item->text(), stepsForStepId(id));
}

void SimulationWizard::runFullPipeline()
{
    // The whole chain for the selected template, in order. MainWindow runs
    // the steps strictly sequentially and stops at the first failing one.
    const QVector<StepId> ids = stepsForTemplate(m_template->currentIndex());
    QVector<WorkflowStep> all;
    const QStringList names = {
        tr("Protein in water"), tr("Protein + ligand"),
        tr("Membrane bilayer"), tr("Polymer in water")};
    for (StepId id : ids) {
        writeMdpForStep(id);
        all.append(stepsForStepId(id));
    }
    emit pipelineReady(tr("Full pipeline — %1").arg(names.value(m_template->currentIndex())), all);
    accept();
}
