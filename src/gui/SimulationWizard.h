#pragma once

#include <QDialog>

#include "core/JobManager.h"   // WorkflowStep

class CoreBridge;
class JobManager;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QListWidget;

/**
 * @brief The full biomolecular simulation pipeline as a guided dialog.
 *
 * System templates (F2) select which steps the pipeline contains:
 *   - Protein in water   (the classic 8-step lysozyme workflow)
 *   - Protein + ligand   (adds ligand preparation + structure merge)
 *   - Membrane (bilayer) (adds protein insertion into a bilayer)
 *   - Polymer in water   (skips pdb2gmx — polymers have no force-field build)
 *
 * Fixed step pages:
 *   pdb2gmx → ligand → membrane-insert → box → solvate → ions →
 *   minimisation → NVT → NPT → production MD
 *
 * Every step can be run alone, or the whole chain can be queued; the
 * WorkflowRunner stops at the first failing step. Production MD supports
 * resuming from a checkpoint (-cpi).
 */
class SimulationWizard : public QDialog
{
    Q_OBJECT

public:
    SimulationWizard(CoreBridge *core, JobManager *jobs, const QString &projectDir,
                     const QString &gmxPath, QWidget *parent = nullptr);

signals:
    //! Emitted once per user action with the (ordered) steps to execute.
    //! MainWindow feeds them to the sequential WorkflowRunner.
    void pipelineReady(const QString &pipelineName, const QVector<WorkflowStep> &steps);

private slots:
    void templateChanged(int index);
    void runCurrentStep();
    void runFullPipeline();

private:
    enum StepId {
        StepPdb2gmx = 0, StepLigand, StepMembraneInsert, StepBox, StepSolvate,
        StepIons, StepMinim, StepNvt, StepNpt, StepMd, StepCount
    };

    QVector<StepId> stepsForTemplate(int templateIndex) const;
    QVector<WorkflowStep> stepsForStepId(StepId id) const;
    QVector<WorkflowStep> stepsForPage(int page) const;
    void rebuildStepList();
    void updatePreview();
    void writeMdpForStep(StepId id);
    QString browseInput(QLineEdit *field, const QString &title);

    CoreBridge *m_core = nullptr;
    JobManager *m_jobs = nullptr;
    QString m_dir;
    QString m_gmx;

    QComboBox *m_template = nullptr;
    QListWidget *m_stepList = nullptr;
    QStackedWidget *m_pages = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_runStepBtn = nullptr;
    QPushButton *m_runAllBtn = nullptr;

    // pdb2gmx page
    QLineEdit *m_pdbInput = nullptr;
    QComboBox *m_forceField = nullptr;
    QComboBox *m_waterModel = nullptr;
    // ligand page
    QLineEdit *m_ligandInput = nullptr;
    // membrane page
    QLineEdit *m_membraneInput = nullptr;
    // box page
    QComboBox *m_boxType = nullptr;
    QLineEdit *m_boxDistance = nullptr;
    QLineEdit *m_polymerInput = nullptr;
    // ions page
    QLineEdit *m_pName = nullptr;
    QLineEdit *m_nName = nullptr;
    // production page
    QCheckBox *m_resumeCheckpoint = nullptr;
};
