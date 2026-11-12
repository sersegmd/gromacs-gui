#include "MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/CoreBridge.h"
#include "core/ErrorHints.h"
#include "core/JobManager.h"
#include "gui/AnalysisDialog.h"
#include "gui/ConsolePanel.h"
#include "gui/JobMonitorPanel.h"
#include "gui/MdpEditorPanel.h"
#include "gui/ProjectPanel.h"
#include "gui/ResourceEstimatorDialog.h"
#include "gui/SimulationWizard.h"
#include "gui/ViewerWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Molecular Studio — Molecular Dynamics Workspace"));

    m_core = new CoreBridge(this);
    m_jobs = new JobManager(m_core, this);
    buildCentralArea();
    buildDocks();
    buildMenus();
    setDockNecessaryProperties();
    connectPipeline();
    detectGromacs();
    detectGpu();
    loadSettings();

    // Stub-mode awareness: without the Rust core, file parsing and job
    // execution are disabled — say it loudly instead of failing silently.
    if (!m_core->coreAvailable()) {
        m_stubBanner = new QLabel(
            tr("⚠ The simulation engine is not linked — structure display, "
               "simulations and analysis are disabled. Please reinstall the "
               "application (see the installation guide)."),
            this);
        m_stubBanner->setStyleSheet(
            QStringLiteral("background:#8b1a1a; color:#fff; padding:8px;"
                           " font-weight:bold;"));
        m_stubBanner->setWordWrap(true);
        m_stubBanner->setAlignment(Qt::AlignCenter);
        // grey out everything that needs the core so failures can't confuse
        for (QAction *action : m_coreActions)
            action->setEnabled(false);
    }

    statusBar()->showMessage(
        tr("Ready · Simulation engine: %1%2")
            .arg(m_gmxPath.isEmpty() ? tr("not found") : tr("available"),
                 m_gpuName.isEmpty() ? QString()
                                     : tr(" · GPU acceleration: %1").arg(m_gpuName)),
        15000);
    auto *statusCore = new QLabel(m_core->coreAvailable()
                                      ? tr("Engine linked")
                                      : tr("Engine not linked — disabled features"),
                                  this);
    if (!m_core->coreAvailable()) {
        statusCore->setStyleSheet(QStringLiteral("color:#ffb0b0; font-weight:bold;"));
        statusCore->setToolTip(tr("The simulation engine library was not found next to "
                                  "the application. Reinstall the application."));
    }
    statusBar()->addPermanentWidget(statusCore);
}

void MainWindow::detectGpu()
{
    // Optional GPU detection: enables the "-nb gpu" advice in the estimator.
    QProcess probe;
    probe.start(QStringLiteral("nvidia-smi"),
                {QStringLiteral("--query-gpu=name"), QStringLiteral("--format=csv,noheader")});
    if (probe.waitForFinished(2000))
        m_gpuName = QString::fromUtf8(probe.readAllStandardOutput().trimmed());
}

void MainWindow::detectGromacs()
{
    m_gmxPath = m_core->detectGmx();
    if (m_gmxPath.isEmpty()) {
        // fall back to plain "gmx" resolved through PATH by the shell
        m_gmxPath = QStringLiteral("gmx");
    }
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New Project…"), this, &MainWindow::newProject,
                        QKeySequence::New);
    fileMenu->addAction(tr("&Open Project…"), this, &MainWindow::openProject,
                        QKeySequence::Open);
    QAction *openStruct = fileMenu->addAction(tr("Open &Structure…"), this,
                                              &MainWindow::openStructureFile,
                                              QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    openStruct->setStatusTip(tr("Open any .pdb/.gro on disk in the 3D viewer "
                                "(works outside the project folder)"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), qApp, &QApplication::quit, QKeySequence::Quit);

    QMenu *simMenu = menuBar()->addMenu(tr("&Simulation"));
    QAction *wizardAction = simMenu->addAction(tr("&New simulation setup…"), this,
                                               &MainWindow::openWizard,
                                               QKeySequence(Qt::CTRL | Qt::Key_N));
    simMenu->addSeparator();
    QAction *runAction = simMenu->addAction(tr("&Analysis dashboard…"), this,
                                            &MainWindow::openAnalysis);
    runAction->setStatusTip(tr("Energy and trajectory reports plotted from your "
                               "simulation output"));
    QAction *loadTrjAction = simMenu->addAction(tr("&Play trajectory in viewer…"), this,
                                                &MainWindow::loadTrajectoryIntoViewer,
                                                QKeySequence(Qt::CTRL | Qt::Key_T));
    QAction *exportFramesAction =
        simMenu->addAction(tr("Prepare &movie frames from a trajectory…"), this,
                           &MainWindow::exportFramesWithTrjconv);
    exportFramesAction->setStatusTip(
        tr("Convert a raw trajectory into a frame sequence the 3D viewer can play"));
    m_coreActions << wizardAction << runAction
                  << loadTrjAction << exportFramesAction;

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    QAction *estimatorAction = toolsMenu->addAction(tr("&Resource estimator…"), this,
                                                    &MainWindow::openResourceEstimator,
                                                    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    m_coreActions << estimatorAction;

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_projectPanel->toggleViewAction());
    viewMenu->addAction(m_mdpEditor->toggleViewAction());
    viewMenu->addAction(m_jobMonitor->toggleViewAction());
    viewMenu->addAction(m_console->toggleViewAction());

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Load sample structure (lysozyme)"), this,
                        &MainWindow::loadDemoStructure);
    helpMenu->addAction(tr("دليل المستخدم (User Guide)"), this, &MainWindow::showUserGuide);
    helpMenu->addAction(tr("&About Molecular Studio"), this, &MainWindow::about);
}

void MainWindow::buildDocks()
{
    m_projectPanel = new ProjectPanel(this);
    m_mdpEditor    = new MdpEditorPanel(this);
    m_mdpEditor->setCore(m_core);
    m_jobMonitor   = new JobMonitorPanel(this);
    m_console      = new ConsolePanel(this);

    // Double-clicking a file in the tree opens it where it belongs:
    // .mdp → editor, .gro/.pdb → 3D viewer, .xvg → plotted to console info.
    connect(m_projectPanel, &ProjectPanel::fileSelected, this,
            [this](const QString &path) {
                if (path.endsWith(QStringLiteral(".mdp")))
                    m_mdpEditor->openFile(path);
                else if (path.endsWith(QStringLiteral(".gro"))
                         || path.endsWith(QStringLiteral(".pdb")))
                    showStructureFile(path);
            });
    connect(m_mdpEditor, &MdpEditorPanel::fileSaved, this,
            [this](const QString &path) { m_projectPanel->refresh(); });
}

void MainWindow::setDockNecessaryProperties()
{
    m_projectPanel->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_projectPanel->setFeatures(QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectPanel);

    m_mdpEditor->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_mdpEditor);

    m_jobMonitor->setFeatures(QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetFloatable);
    m_console->setFeatures(QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, m_console);
    addDockWidget(Qt::BottomDockWidgetArea, m_jobMonitor);
    tabifyDockWidget(m_console, m_jobMonitor);
    m_jobMonitor->raise();
}

void MainWindow::connectPipeline()
{
    // JobManager → JobMonitor + Console (live streaming)
    connect(m_jobs, &JobManager::jobSubmitted, this,
            [this](int id, const QString &label, const QString &command) {
                Q_UNUSED(command);   // internals stay out of the visible log
                m_jobMonitor->addJob(id, label, command);
                m_console->appendInfo(tr("▶ Task #%1 started: %2").arg(id).arg(label));
                m_jobMonitor->raise();
            });
    connect(m_jobs, &JobManager::jobOutput, this,
            [this](int id, const QString &label, const QString &line, bool isError) {
                Q_UNUSED(label);
                // Keep a bounded stderr tail per job for the smart diagnosis.
                QString &tail = m_jobErrors[id];
                tail += line + QLatin1Char('\n');
                const int cap = 8000;
                if (tail.size() > cap)
                    tail = tail.right(cap);
                if (isError)
                    m_console->appendError(line);
                else
                    m_console->appendRaw(line);
            });
    connect(m_jobs, &JobManager::jobFinished, this,
            [this](int id, const QString &label, const QString &state, int exitCode) {
                const bool ok = state == QStringLiteral("completed");
                m_jobMonitor->setJobState(id, ok ? JobMonitorPanel::JobState::Completed
                                                 : JobMonitorPanel::JobState::Failed);
                if (ok) {
                    m_console->appendInfo(tr("✔ Task #%1 (%2) completed").arg(id).arg(label));
                } else {
                    m_console->appendError(
                        tr("Task #%1 (%2) %3 — exit code %4")
                            .arg(id).arg(label).arg(state).arg(exitCode));
                    // F5 — smart error translation: show cause + suggested fix.
                    const QString hint = ErrorHints::explain(m_jobErrors.take(id));
                    if (!hint.isEmpty()) {
                        m_console->appendInfo(hint);
                        m_console->raise();
                    }
                }
                m_jobErrors.remove(id);
                m_projectPanel->refresh();
            });
}

void MainWindow::buildCentralArea()
{
    m_viewer = new ViewerWidget(this);

    // Playback toolbar (F6): trajectory transport controls under the viewer.
    auto *transport = new QWidget(this);
    auto *row = new QHBoxLayout(transport);
    row->setContentsMargins(6, 2, 6, 2);

    auto *prevBtn = new QPushButton(QStringLiteral("⏮"), transport);
    m_playButton = new QPushButton(QStringLiteral("▶"), transport);
    auto *nextBtn = new QPushButton(QStringLiteral("⏭"), transport);
    m_frameSlider = new QSlider(Qt::Horizontal, transport);
    m_frameSlider->setRange(0, 0);
    auto *loadTrjBtn = new QPushButton(tr("Load trajectory…"), transport);
    auto *exportFramesBtn = new QPushButton(tr("Prepare frames…"), transport);
    exportFramesBtn->setToolTip(
        tr("Convert a raw trajectory into a frame sequence the viewer can play"));
    m_speedCombo = new QComboBox(transport);
    m_speedCombo->addItems({QStringLiteral("2x"), QStringLiteral("1x"),
                            QStringLiteral("0.5x"), QStringLiteral("0.25x")});
    m_speedCombo->setCurrentIndex(1);
    m_solventCheck = new QCheckBox(tr("Show solvent"), transport);
    m_solventCheck->setChecked(true);
    m_solventCheck->setToolTip(
        tr("Uncheck to drop water atoms on load — large solvated frames "
           "render much faster (re-load the trajectory to apply)"));
    auto *styleCombo = new QComboBox(transport);
    styleCombo->addItems({tr("Ball & Stick"), tr("Space-Filling"),
                          tr("Protein Backbone"), tr("Protein Ribbon")});
    styleCombo->setToolTip(tr("Structure rendering style"));

    row->addWidget(prevBtn);
    row->addWidget(m_playButton);
    row->addWidget(nextBtn);
    row->addWidget(m_frameSlider, 1);
    row->addWidget(styleCombo);
    row->addWidget(m_solventCheck);
    row->addWidget(new QLabel(tr("Speed:"), transport));
    row->addWidget(m_speedCombo);
    row->addWidget(loadTrjBtn);
    row->addWidget(exportFramesBtn);

    auto *viewerPane = new QWidget(this);
    auto *paneLayout = new QVBoxLayout(viewerPane);
    paneLayout->setContentsMargins(0, 0, 0, 0);
    paneLayout->addWidget(m_viewer, 1);

    // System info strip (addition 5): title · atom count · box, next to the viewer.
    m_infoLabel = new QLabel(tr("No structure loaded"), viewerPane);
    m_infoLabel->setStyleSheet(
        QStringLiteral("color:#555; padding:2px 8px; background:#f4f4f4;"));
    m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    paneLayout->addWidget(m_infoLabel);
    paneLayout->addWidget(transport);

    m_workspace = new QTabWidget(this);
    m_workspace->setDocumentMode(true);
    m_workspace->addTab(viewerPane, tr("3D Viewer"));
    setCentralWidget(m_workspace);

    connect(prevBtn, &QPushButton::clicked, m_viewer, &ViewerWidget::prevFrame);
    connect(nextBtn, &QPushButton::clicked, m_viewer, &ViewerWidget::nextFrame);
    connect(m_playButton, &QPushButton::clicked, m_viewer, &ViewerWidget::playPause);
    connect(m_frameSlider, &QSlider::valueChanged, m_viewer, &ViewerWidget::setFrame);
    connect(m_viewer, &ViewerWidget::frameChanged, this,
            [this](int index, int total) {
                if (m_frameSlider->maximum() != qMax(0, total - 1))
                    m_frameSlider->setRange(0, qMax(0, total - 1));
                m_frameSlider->blockSignals(true);
                m_frameSlider->setValue(index);
                m_frameSlider->blockSignals(false);
            });
    connect(m_viewer, &ViewerWidget::playingChanged, this,
            [this](bool playing) {
                m_playButton->setText(playing ? QStringLiteral("⏸") : QStringLiteral("▶"));
            });
    connect(m_speedCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
                const int ms = index == 0 ? 75 : index == 1 ? 150
                             : index == 2 ? 300 : 600;
                m_viewer->setSpeedMs(ms);
            });
    connect(loadTrjBtn, &QPushButton::clicked, this,
            &MainWindow::loadTrajectoryIntoViewer);
    connect(exportFramesBtn, &QPushButton::clicked, this,
            &MainWindow::exportFramesWithTrjconv);
    connect(styleCombo, &QComboBox::currentIndexChanged, m_viewer,
            &ViewerWidget::setDisplayMode);
}

void MainWindow::loadProject(const QString &dir)
{
    m_projectDir = dir;
    m_projectPanel->loadProject(dir);
    m_console->appendInfo(tr("Project: %1").arg(dir));
    statusBar()->showMessage(tr("Project: %1").arg(dir));
    setWindowTitle(tr("Molecular Studio — %1").arg(dir));
}

void MainWindow::newProject()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Create Project In Folder"));
    if (!dir.isEmpty())
        loadProject(dir);
}

void MainWindow::openProject()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Project Folder"));
    if (!dir.isEmpty())
        loadProject(dir);
}

void MainWindow::openWizard()
{
    if (!m_wizard) {
        m_wizard = new SimulationWizard(m_core, m_jobs, m_projectDir, m_gmxPath, this);
        connect(m_wizard, &SimulationWizard::pipelineReady, this,
                [this](const QString &name, const QVector<WorkflowStep> &steps) {
                    runPipeline(name, steps);
                });
    }
    m_wizard->show();
    m_wizard->raise();
    m_wizard->activateWindow();
}

void MainWindow::openAnalysis()
{
    if (!m_analysis) {
        m_analysis = new AnalysisDialog(m_core, m_projectDir, m_gmxPath, this);
        connect(m_analysis,
                static_cast<void (AnalysisDialog::*)(const QString &, const QString &,
                                                     const QString &, const QString &,
                                                     const QString &)>(&AnalysisDialog::commandReady),
                this,
                [this](const QString &label, const QString &command, const QString &workdir,
                       const QString &stdinData, const QString &xvgHint) {
                    runCommand(label, command, workdir, stdinData, xvgHint);
                });
    }
    m_analysis->show();
    m_analysis->raise();
    m_analysis->activateWindow();
}

void MainWindow::runCommand(const QString &label, const QString &command,
                            const QString &workdir, const QString &stdinData,
                            const QString &xvgHint)
{
    const int id = m_jobs->submit(label, command,
                                  workdir.isEmpty() ? m_projectDir : workdir, stdinData);
    if (id < 0) {
        m_console->appendError(tr("Cannot submit: the simulation engine is not linked."));
        return;
    }
    if (!xvgHint.isEmpty()) {
        // after completion, load the xvg into the analysis plot
        connect(m_jobs, &JobManager::jobFinished, this,
                [this, xvgHint, id](int finishedId, const QString &, const QString &state, int) {
                    if (finishedId != id)
                        return;
                    if (state == QStringLiteral("completed"))
                        m_analysis->jobCompleted(xvgHint);
                }, Qt::SingleShotConnection);
    }
}

void MainWindow::openResourceEstimator()
{
    ResourceEstimatorDialog dialog(this);
    dialog.exec();
}

void MainWindow::runPipeline(const QString &name, const QVector<WorkflowStep> &steps)
{
    if (steps.isEmpty())
        return;
    m_console->raise();
    m_console->appendInfo(tr("Pipeline «%1» — %2 steps, executed one after another.")
                              .arg(name).arg(steps.size()));

    auto *runner = new WorkflowRunner(m_jobs, this);
    connect(runner, &WorkflowRunner::stepStarted, this,
            [this](int index, const QString &label) {
                m_console->appendInfo(tr("— Step %1: %2")
                                          .arg(index + 1).arg(label));
            });
    connect(runner, &WorkflowRunner::stepBlocked, this,
            [this](int index, const QString &label, const QString &missingFile) {
                m_console->appendError(
                    tr("Step %1 (%2) cannot start: required file %3 is missing "
                       "in the project folder. Run the earlier step that "
                       "produces it first.")
                        .arg(index + 1).arg(label, missingFile));
            });
    connect(runner, &WorkflowRunner::stepFinished, this,
            [this](int index, const QString &label, bool ok) {
                if (ok)
                    m_console->appendInfo(tr("✔ Step %1 (%2) done")
                                              .arg(index + 1).arg(label));
                else
                    m_console->appendWarning(
                        tr("✖ Step %1 (%2) failed — the pipeline stopped here. "
                           "Read the log above for the cause and fix, then "
                           "re-run the remaining steps.")
                            .arg(index + 1).arg(label));
            });
    connect(runner, &WorkflowRunner::pipelineFinished, this,
            [this](const QString &pipelineName, bool ok) {
                if (ok)
                    m_console->appendInfo(tr("Pipeline «%1» finished successfully.")
                                              .arg(pipelineName));
                else
                    m_console->appendWarning(
                        tr("Pipeline «%1» finished early (see failures above).")
                            .arg(pipelineName));
                m_projectPanel->refresh();
            });
    runner->run(steps, name);
}

void MainWindow::loadTrajectoryIntoViewer()
{
    if (!m_core->coreAvailable()) {
        m_console->appendError(
            tr("Trajectory load skipped: the simulation engine is not linked — "
               "please reinstall the application."));
        m_console->raise();
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load frame sequence"),
        m_projectDir, tr("Trajectory containers (*.pdb);;Structure files (*.gro *.pdb)"));
    if (path.isEmpty())
        return;
    const bool skipSolvent = m_solventCheck && !m_solventCheck->isChecked();
    const QVector<StructureData> frames = m_core->parseTrajectory(path, skipSolvent);
    if (frames.isEmpty()) {
        const QString err = m_core->lastError();
        if (err == QStringLiteral("cannot read file")) {
            m_console->appendError(tr("Cannot read %1 — check the file exists "
                                      "and is readable.").arg(path));
        } else if (err == QStringLiteral("core-not-linked")) {
            m_console->appendError(tr("The simulation engine is not linked — "
                                      "please reinstall the application."));
        } else {
            m_console->appendWarning(
                tr("No frames could be read from %1. If this is a raw "
                   "trajectory, use Simulation → «Prepare movie frames from a "
                   "trajectory…» first — it converts the file into a frame "
                   "sequence the viewer can play.").arg(path));
        }
        m_console->raise();
        return;
    }
    m_workspace->setCurrentWidget(m_viewer);
    m_viewer->setTrajectory(frames);
    m_console->appendInfo(tr("Trajectory loaded: %1 frames from %2%3")
                              .arg(frames.size())
                              .arg(QFileInfo(path).fileName())
                              .arg(skipSolvent ? tr(" (solvent hidden)") : QString()));
    updateViewerInfo(tr("%1 · %2 frames · %3 atoms/frame%4")
                         .arg(QFileInfo(path).fileName())
                         .arg(frames.size())
                         .arg(frames.first().natoms)
                         .arg(skipSolvent ? tr(" · solvent hidden") : QString()));
}

void MainWindow::showStructureFile(const QString &path)
{
    const StructureData data = m_core->parseStructure(path);
    if (!data.atoms.isEmpty()) {
        m_workspace->setCurrentWidget(m_viewer);
        m_viewer->setStructure(data);
        m_console->appendInfo(tr("Opened %1 (%2 atoms)")
                                  .arg(path).arg(data.natoms));
        updateViewerInfo(tr("%1 · %2 atoms · box %3×%4×%5 nm")
                             .arg(data.title.isEmpty()
                                      ? QFileInfo(path).fileName()
                                      : data.title.left(60))
                             .arg(data.natoms)
                             .arg(data.box[0], 0, 'f', 1)
                             .arg(data.box[1], 0, 'f', 1)
                             .arg(data.box[2], 0, 'f', 1));
        return;
    }
    const QString err = m_core->lastError();
    if (err == QStringLiteral("core-not-linked")) {
        m_console->appendError(
            tr("Cannot open %1: the simulation engine is not linked — "
               "please reinstall the application.")
                .arg(path));
    } else if (err == QStringLiteral("cannot read file")) {
        m_console->appendError(
            tr("Cannot read %1 — check the file exists and is readable.")
                .arg(path));
    } else {
        m_console->appendWarning(
            tr("Could not parse structure: %1 (%2)").arg(path, err));
    }
    m_console->raise();
}

void MainWindow::openStructureFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open structure file"),
        QDir::homePath(),
        tr("Structure files (*.pdb *.gro *.ent);;All files (*)"));
    if (!path.isEmpty())
        showStructureFile(path);
}

void MainWindow::exportFramesWithTrjconv()
{
    if (!m_core->coreAvailable()) {
        m_console->appendError(
            tr("Export skipped: the simulation engine is not linked — "
               "please reinstall the application."));
        m_console->raise();
        return;
    }
    const QString tpr = QFileDialog::getOpenFileName(
        this, tr("Select the simulation run file"), m_projectDir,
        tr("Simulation run file (*.tpr);;All files (*)"));
    if (tpr.isEmpty())
        return;
    const QString traj = QFileDialog::getOpenFileName(
        this, tr("Select raw trajectory (.xtc or .trr)"),
        QFileInfo(tpr).absolutePath(),
        tr("Trajectories (*.xtc *.trr);;All files (*)"));
    if (traj.isEmpty())
        return;
    const QString out =
        QDir(m_projectDir.isEmpty() ? QFileInfo(traj).absolutePath() : m_projectDir)
            .filePath(QStringLiteral("frames.pdb"));
    const QString command = QStringLiteral(
        "%1 trjconv -s %2 -f %3 -o %4 -dt 50")
        .arg(m_gmxPath, QFileInfo(tpr).fileName(), QFileInfo(traj).fileName(),
             out);
    // "System" answers trjconv's group prompt so the export runs unattended.
    const int id = m_jobs->submit(tr("Movie frames preparation"), command,
                                  QFileInfo(tpr).absolutePath(),
                                  QStringLiteral("0\n"));
    if (id < 0) {
        m_console->appendError(tr("Cannot submit: the simulation engine is not linked."));
        return;
    }
    m_console->appendInfo(tr("Exporting frames to %1 — the viewer will load "
                             "them automatically when the job completes.")
                              .arg(out));
    // When the export completes, load the produced frames.pdb right away.
    connect(m_jobs, &JobManager::jobFinished, this,
            [this, out, id](int finishedId, const QString &,
                            const QString &state, int) {
                if (finishedId != id)
                    return;
                if (state == QStringLiteral("completed")) {
                    const bool skipSolvent =
                        m_solventCheck && !m_solventCheck->isChecked();
                    const QVector<StructureData> frames =
                        m_core->parseTrajectory(out, skipSolvent);
                    if (!frames.isEmpty()) {
                        m_workspace->setCurrentWidget(m_viewer);
                        m_viewer->setTrajectory(frames);
                        m_console->appendInfo(
                            tr("Trajectory loaded: %1 frames from %2")
                                .arg(frames.size())
                                .arg(QFileInfo(out).fileName()));
                        updateViewerInfo(
                            tr("%1 · %2 frames · %3 atoms/frame%4")
                                .arg(QFileInfo(out).fileName())
                                .arg(frames.size())
                                .arg(frames.first().natoms)
                                .arg(skipSolvent ? tr(" · solvent hidden")
                                                 : QString()));
                    } else {
                        m_console->appendWarning(
                            tr("Export finished but %1 could not be parsed "
                               "into frames.").arg(out));
                        m_console->raise();
                    }
                }
            },
            Qt::SingleShotConnection);
}

void MainWindow::loadDemoStructure()
{
    const QString path = demoResourcePath(QStringLiteral("1lyz.pdb"));
    if (path.isEmpty()) {
        m_console->appendWarning(
            tr("Demo file 1lyz.pdb not found — expected in resources/demo "
               "next to the application."));
        m_console->raise();
        return;
    }
    m_console->appendInfo(tr("Loading demo: %1").arg(path));
    showStructureFile(path);
    // Also offer the bundled trajectory for the animation controls.
    const QString framesPath = demoResourcePath(QStringLiteral("1lyz_frames.pdb"));
    if (!framesPath.isEmpty() && m_core->coreAvailable()) {
        const QVector<StructureData> frames = m_core->parseTrajectory(framesPath);
        if (!frames.isEmpty()) {
            m_viewer->setTrajectory(frames);
            m_frameSlider->setRange(0, qMax(0, frames.size() - 1));
            m_console->appendInfo(
                tr("Demo trajectory attached: %1 frames — press ▶ to play.")
                    .arg(frames.size()));
        }
    }
}

QString MainWindow::demoResourcePath(const QString &fileName) const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/resources/demo/") + fileName,
        appDir + QStringLiteral("/../resources/demo/") + fileName,
        appDir + QStringLiteral("/../../resources/demo/") + fileName,
        appDir + QStringLiteral("/../../demo/") + fileName,
        appDir + QStringLiteral("/../../../resources/demo/") + fileName,
    };
    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate))
            return candidate;
    }
    return QString();
}

void MainWindow::updateViewerInfo(const QString &text)
{
    if (m_infoLabel)
        m_infoLabel->setText(text);
}

void MainWindow::showUserGuide()
{
    // Built-in Arabic user guide (F8) — the workflow from structure to analysis.
    const QString guide = QStringLiteral(
        "<h2>دليل استخدام Molecular Studio</h2>"
        "<p>برنامج Molecular Studio بيئة عمل متكاملة للمحاكاة الجزيئية، تنفّذ "
        "الخطوات العلمية القياسية عبر محرك المحاكاة المثبت على جهازك.</p>"
        "<h3>1) سير العمل الكامل (Simulation ← New simulation setup)</h3>"
        "<p>اختر قالب النظام (بروتين في ماء / بروتين + ليغاند / غشاء دهني / بوليمر)، "
        "ثم نفّذ الخطوات بالترتيب من تحضير البنية حتى الإنتاج. الخطوات تُنفَّذ "
        "<b>واحدة تلو الأخرى</b>، وكل خطوة تتحقق من وجود ملفاتها المطلوبة قبل "
        "الانطلاق؛ إذا فشلت خطوة تتوقف السلسلة فوراً مع رسالة تشرح السبب والحل. "
        "يمكنك تشغيل خطوة واحدة أو السلسلة كاملة بـ «Run full pipeline».</p>"
        "<h3>2) مجموعات الاختيار</h3>"
        "<p>في نافذة التحليل اختر المجموعتين (Protein, Backbone, Water…) وسيبلغ "
        "البرنامج المحرك باختياراتك تلقائياً دون تدخل منك.</p>"
        "<h3>3) تحليل النتائج</h3>"
        "<p>نافذة «Analysis dashboard» تكتشف ملفات بياناتك تلقائياً (طاقة، مدخلات "
        "التشغيل، المسارات) وتعرض التقارير المرسومة فوراً: الطاقة، الانحراف "
        "الجذري المتوسط، نصف قطر الجردانة، التوزيع الشعاعي، المساحة السطحية، "
        "الانتشار، والبنية الثانوية. زر «Compare multiple runs» يعرض عدة "
        "تشغيلات على رسم واحد، وزر «Export plot» يصدّر الرسم بجودة النشر.</p>"
        "<h3>4) تشغيل المسار في العارض ثلاثي الأبعاد</h3>"
        "<p>استخدم <b>Simulation ← «Prepare movie frames from a trajectory»</b>: "
        "سيحوّل المسار الخام إلى سلسلة إطارات ويحمّلها في العارض تلقائياً عند "
        "الانتهاء. أو حمّل ملفاً جاهزاً من زر «Load trajectory…» وشغّله "
        "بالأنيميشن (⏮ ▶ ⏭ + سرعة). اختر أسلوب العرض من القائمة المنسدلة: "
        "كرات وعصي، حشو كروي، عمود فقري للبروتين، أو <b>شريط البروتين "
        "(Ribbon)</b> الذي يُظهر البنية ثلاثية الأبعاد بوضوح. قائمة Help ← "
        "«Load sample structure» تفتح جزيء ليزوزيم بضغطة واحدة.</p>"
        "<h3>5) عند فشل خطوة</h3>"
        "<p>يظهر في الـ Console تشخيص ذكي: سبب الخطأ الشائع والحل المقترح، فوق "
        "مخرجات المحرك الأصلية الكاملة. أما إذا ظهر شريط أحمر أعلى النافذة أو "
        "أشارة شريط الحالة إلى أن المحرك غير مرتبط، فأعد تثبيت البرنامج كما في "
        "دليل التثبيت.</p>"
        "<h3>6) نصائح إنتاجية</h3>"
        "<p>استخدم «Resource estimator» لتقدير الذاكرة والوقت، وفعّل استكمال "
        "التشغيل من نقطة حفظ (خيار الـ checkpoint في خطوة الإنتاج) لاستكمال "
        "التشغيلات الطويلة بعد انقطاعها، وإذا كان لديك معرّف رسومي فسيُكتشف "
        "تلقائياً لتسريع الحساب.</p>");
    QMessageBox box(this);
    box.setWindowTitle(tr("دليل المستخدم — Molecular Studio"));
    box.setTextFormat(Qt::RichText);
    box.setText(guide);
    box.setIcon(QMessageBox::Information);
    box.exec();
}

void MainWindow::about()
{
    QMessageBox::information(this, tr("About Molecular Studio"),
        tr("<b>Molecular Studio %1</b><br>"
           "A professional desktop workspace for molecular dynamics "
           "simulations.<br><br>"
           "Molecular Studio drives the established simulation engine "
           "installed on your system; it does not replace or modify it.")
            .arg(QStringLiteral(MS_VERSION)));
}

void MainWindow::loadSettings()
{
    QSettings s;
    const QByteArray geom = s.value("window/geometry").toByteArray();
    if (!geom.isNull())
        restoreGeometry(geom);
    else
        resize(1400, 880);
    restoreState(s.value("window/state").toByteArray());
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    event->accept();
}

void MainWindow::loadNewestStructureForDemo()
{
    // Prefer the bare protein for a readable demo render; fall back to the
    // most recent structure file in the project.
    QString path = QDir(m_projectDir).filePath(QStringLiteral("protein.gro"));
    if (!QFile::exists(path)) {
        const QFileInfoList structs =
            QDir(m_projectDir).entryInfoList({QStringLiteral("*.gro"), QStringLiteral("*.pdb")},
                                             QDir::Files, QDir::Time);
        if (structs.isEmpty()) {
            m_console->appendWarning(tr("Demo: no structure file in project."));
            return;
        }
        path = structs.first().absoluteFilePath();
    }
    const StructureData data = m_core->parseStructure(path);
    if (!data.atoms.isEmpty()) {
        m_workspace->setCurrentWidget(m_viewer);
        m_viewer->setStructure(data);
        m_console->appendInfo(tr("Viewer: loaded %1 (%2 atoms)")
                                  .arg(QFileInfo(path).fileName())
                                  .arg(data.natoms));
    }
}

void MainWindow::runRealDemoCommand()
{
    // Runs a real, fast GROMACS command through the Rust core so the
    // console shows genuine live output (used by the demo harness).
    const QString command = QStringLiteral("%1 --version").arg(m_gmxPath);
    m_console->raise();
    runCommand(tr("Engine check"), command, m_projectDir);
    // the job handler raises the Jobs dock; bring the live output back up
    QTimer::singleShot(2000, this, [this]() { m_console->raise(); });
}
