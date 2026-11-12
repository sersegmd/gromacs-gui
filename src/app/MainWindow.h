#pragma once

#include <QHash>
#include <QMainWindow>
#include <QString>

#include "core/JobManager.h"   // WorkflowStep

class QTabWidget;
class QSlider;
class QPushButton;
class QComboBox;
class QCheckBox;
class QLabel;
class QToolButton;
class ProjectPanel;
class MdpEditorPanel;
class JobMonitorPanel;
class ConsolePanel;
class ViewerWidget;
class CoreBridge;
class JobManager;
class SimulationWizard;
class AnalysisDialog;

/**
 * @brief Top-level application window.
 *
 * Layout:
 *   - Left dock:   ProjectPanel   (project tree / file browser)
 *   - Right dock:  MdpEditorPanel (parameter editor with templates + linter)
 *   - Bottom dock: JobMonitorPanel (running tasks) tab-stacked with
 *                  ConsolePanel (combined log)
 *   - Center:      3D molecular viewer (OpenGL, CPK colours, ribbon mode)
 *
 * Simulation pipelines arrive as ordered WorkflowStep lists (wizard) and are
 * executed strictly sequentially by WorkflowRunner, with pre-flight checks
 * on every required input file.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    //! Loads a project directory and refreshes the tree.
    void loadProject(const QString &dir);

    //! Paths/objects exposed for the demo & screenshot harness.
    QString projectDir() const { return m_projectDir; }
    QString gmxPath() const { return m_gmxPath; }
    ViewerWidget *viewer() const { return m_viewer; }
    MdpEditorPanel *mdpEditor() const { return m_mdpEditor; }
    JobManager *jobManager() const { return m_jobs; }

    //! Demo/screenshot harness entry points (wrap the private slots).
    void openWizardForDemo() { openWizard(); }
    void openAnalysisForDemo() { openAnalysis(); }
    void loadNewestStructureForDemo();
    void runRealDemoCommand();

public slots:
    //! Runs a single command through the core (used by the demo harness).
    void runCommand(const QString &label, const QString &command,
                    const QString &workdir = QString(),
                    const QString &stdinData = QString(), const QString &xvgHint = QString());

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newProject();
    void openProject();
    void openStructureFile();
    void openWizard();
    void openAnalysis();
    void openResourceEstimator();
    void loadTrajectoryIntoViewer();
    void exportFramesWithTrjconv();
    void loadDemoStructure();
    void showUserGuide();
    void about();

private:
    void buildMenus();
    void buildDocks();
    void buildCentralArea();
    void setDockNecessaryProperties();
    void connectPipeline();
    void detectGromacs();
    void detectGpu();
    void loadSettings();
    void saveSettings();
    void showStructureFile(const QString &path);
    void updateViewerInfo(const QString &text);
    QString demoResourcePath(const QString &fileName) const;
    //! Executes an ordered pipeline sequentially, logging each step.
    void runPipeline(const QString &name, const QVector<WorkflowStep> &steps);

    ProjectPanel    *m_projectPanel   = nullptr;
    MdpEditorPanel  *m_mdpEditor      = nullptr;
    JobMonitorPanel *m_jobMonitor     = nullptr;
    ConsolePanel    *m_console        = nullptr;
    ViewerWidget    *m_viewer         = nullptr;
    QTabWidget      *m_workspace      = nullptr;
    CoreBridge      *m_core           = nullptr;
    JobManager      *m_jobs           = nullptr;
    SimulationWizard *m_wizard        = nullptr;
    AnalysisDialog  *m_analysis       = nullptr;

    QString m_projectDir;
    QString m_gmxPath;
    QString m_gpuName;
    QString m_activeStdin;

    // stub-mode awareness (red banner + disabled core-dependent actions)
    QLabel *m_stubBanner = nullptr;
    QList<QAction *> m_coreActions;

    // system info strip under the 3D viewer (addition 5)
    QLabel *m_infoLabel = nullptr;

    // viewer playback controls (F6)
    QSlider *m_frameSlider = nullptr;
    QPushButton *m_playButton = nullptr;
    QComboBox *m_speedCombo = nullptr;
    QCheckBox *m_solventCheck = nullptr;

    // per-job stderr tail for smart error diagnosis (F5)
    QHash<int, QString> m_jobErrors;
};
