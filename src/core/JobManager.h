#pragma once

#include <QObject>
#include <QQueue>
#include <QString>

class CoreBridge;

struct WorkflowStep {
    QString label;          // human-readable step name
    QString command;        // full shell command line
    QString workdir;
    QStringList requires;   // files (relative to workdir) that must exist
                            // before this step may start
    QString stdinData;      // interactive answers fed to the engine, if any
    QString description;    // one-line plain-language explanation (UI)
};

/**
 * @brief Submits GROMACS jobs through the Rust core and re-emits their
 * output as Qt signals (safe cross-thread via queued invocations).
 */
class JobManager : public QObject
{
    Q_OBJECT

public:
    explicit JobManager(CoreBridge *core, QObject *parent = nullptr);

    //! Submits a command; returns the core job id (-1 in stub mode).
    int submit(const QString &label, const QString &command, const QString &workdir,
               const QString &stdinData = QString());

    //! Cancels the most recent running job with the given id.
    void cancel(int jobId);

signals:
    void jobSubmitted(int jobId, const QString &label, const QString &command);
    void jobOutput(int jobId, const QString &label, const QString &line, bool isError);
    void jobFinished(int jobId, const QString &label, const QString &state, int exitCode);

private slots:
    void deliverOutput(int jobId, const QString &label, const QString &line, bool isError);
    void deliverFinished(int jobId, const QString &label, const QString &state, int exitCode);

private:
    static void coreEventTrampoline(int level, const char *msg, void *user);

    struct JobContext {
        JobManager *manager = nullptr;
        int jobId = 0;
        QString label;
        bool finished = false;
    };

    CoreBridge *m_core = nullptr;
    int m_nextId = 1;   // GUI-side job counter
};

/**
 * @brief Runs a list of WorkflowSteps sequentially through JobManager,
 * aborting the chain when a step fails.
 */
class WorkflowRunner : public QObject
{
    Q_OBJECT

public:
    WorkflowRunner(JobManager *jobs, QObject *parent = nullptr);

    void run(const QVector<WorkflowStep> &steps, const QString &pipelineName);

signals:
    void stepStarted(int index, const QString &label);
    void stepFinished(int index, const QString &label, bool ok);
    //! A step could not start because a required input file is missing.
    void stepBlocked(int index, const QString &label, const QString &missingFile);
    void pipelineFinished(const QString &pipelineName, bool ok);

private:
    void submitNext();

    JobManager *m_jobs = nullptr;
    QVector<WorkflowStep> m_steps;
    QString m_name;
    int m_current = -1;
    int m_jobId = -1;
    bool m_ok = true;
};
