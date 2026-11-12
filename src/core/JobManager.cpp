#include "JobManager.h"

#include "CoreBridge.h"

#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPointer>

// ---------------------------------------------------------------------------
// JobManager — submits GROMACS commands through the Rust core and converts
// its raw C callbacks (fired on Rust worker threads) into Qt signals on the
// GUI thread.
// ---------------------------------------------------------------------------

namespace {

// Per-job context handed to the Rust callback; freed when the control
// "state" event arrives. If the GUI dies first the small context is leaked
// deliberately (QPointer makes delivery a no-op).
struct EventContext {
    QPointer<JobManager> manager;
    int guiId = 0;
    QString label;
};

} // namespace

JobManager::JobManager(CoreBridge *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
}

void JobManager::coreEventTrampoline(int level, const char *msg, void *user)
{
    auto *ctx = static_cast<EventContext *>(user);
    if (!ctx || !ctx->manager)
        return;
    JobManager *manager = ctx->manager.data();
    const int guiId = ctx->guiId;
    const QString label = ctx->label;
    const QString text = QString::fromUtf8(msg ? msg : "");

    if (level == 2) {
        // Control JSON: {"event":"state","state":"completed","exit":0}
        QString state = QStringLiteral("failed");
        int exit = -1;
        const int statePos = text.indexOf("\"state\":\"");
        if (statePos >= 0) {
            const int end = text.indexOf('"', statePos + 9);
            if (end > 0)
                state = text.mid(statePos + 9, end - statePos - 9);
        }
        const int exitPos = text.indexOf("\"exit\":");
        if (exitPos >= 0) {
            const int end = text.indexOf('}', exitPos);
            exit = text.mid(exitPos + 7, end - exitPos - 7).toInt();
        }
        QMetaObject::invokeMethod(manager, [manager, guiId, label, state, exit, ctx]() {
            manager->deliverFinished(guiId, label, state, exit);
            delete ctx;
        }, Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(manager, [manager, guiId, label, text, level]() {
            manager->deliverOutput(guiId, label, text, level == 1);
        }, Qt::QueuedConnection);
    }
}

int JobManager::submit(const QString &label, const QString &command, const QString &workdir,
                       const QString &stdinData)
{
    if (!m_core || !m_core->coreAvailable())
        return -1;

    auto *ctx = new EventContext{QPointer<JobManager>(this), m_nextId, label};
    const int coreId = m_core->submitJob(command, workdir, stdinData,
                                         &JobManager::coreEventTrampoline, ctx);
    if (coreId <= 0) {
        delete ctx;
        return -1;
    }
    emit jobSubmitted(m_nextId, label, command);
    return m_nextId++;
}

void JobManager::deliverOutput(int jobId, const QString &label, const QString &line, bool isError)
{
    emit jobOutput(jobId, label, line, isError);
}

void JobManager::deliverFinished(int jobId, const QString &label, const QString &state, int exitCode)
{
    emit jobFinished(jobId, label, state, exitCode);
}

// ---------------------------------------------------------------------------
// WorkflowRunner
// ---------------------------------------------------------------------------

WorkflowRunner::WorkflowRunner(JobManager *jobs, QObject *parent)
    : QObject(parent)
    , m_jobs(jobs)
{
}

void WorkflowRunner::run(const QVector<WorkflowStep> &steps, const QString &pipelineName)
{
    m_steps = steps;
    m_name = pipelineName;
    m_current = -1;
    m_ok = true;
    m_jobId = -1;
    submitNext();
}

void WorkflowRunner::submitNext()
{
    ++m_current;
    if (m_current >= m_steps.size() || !m_ok) {
        emit pipelineFinished(m_name, m_ok);
        deleteLater();
        return;
    }
    const WorkflowStep &step = m_steps.at(m_current);
    const int stepIndex = m_current;

    // Pre-flight: every required input must exist in the step's workdir.
    // A missing file means an earlier stage never ran (or ran elsewhere) —
    // failing fast here gives a clear message instead of a cryptic
    // "File 'x' does not exist" from the engine midway through the chain.
    for (const QString &req : step.requires) {
        const QString path = QDir(step.workdir.isEmpty() ? QStringLiteral(".") : step.workdir)
                                 .absoluteFilePath(req);
        if (!QFile::exists(path)) {
            m_ok = false;
            emit stepBlocked(stepIndex, step.label, path);
            emit pipelineFinished(m_name, false);
            deleteLater();
            return;
        }
    }

    emit stepStarted(stepIndex, step.label);

    // The core executes every submission on its own thread — submitting the
    // whole chain at once would run the steps in parallel and race. Each
    // next step is therefore submitted only after this one finishes.
    const int jobId = m_jobs->submit(step.label, step.command, step.workdir, step.stdinData);
    m_jobId = jobId;
    if (jobId < 0) {
        m_ok = false;
        emit stepFinished(stepIndex, step.label, false);
        submitNext();
        return;
    }
    connect(m_jobs, &JobManager::jobFinished, this,
            [this, stepIndex, jobId](int finishedId, const QString &label,
                                     const QString &state, int exit) {
                if (finishedId != jobId || finishedId != m_jobId)
                    return;   // some other job's completion
                const bool ok = (state == QStringLiteral("completed") && exit == 0);
                emit stepFinished(stepIndex, label, ok);
                if (!ok)
                    m_ok = false;
                submitNext();
            }, Qt::SingleShotConnection);
}

void JobManager::cancel(int jobId)
{
    if (m_core)
        m_core->cancelJob(jobId);
}
