#pragma once

#include <QDockWidget>
#include <QHash>

class QTableWidget;

/**
 * @brief Bottom dock: table of GROMACS jobs (queued / running / done / failed).
 *
 * In the MVP the rows are filled by the UI itself; once the Rust core is
 * linked, rows are driven by the core's job scheduler via FFI callbacks.
 */
class JobMonitorPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit JobMonitorPanel(QWidget *parent = nullptr);

    enum class JobState { Queued, Running, Completed, Failed };

public slots:
    //! Adds a job row keyed by the GUI job id; returns the row index.
    int addJob(int jobId, const QString &label, const QString &command);
    void setJobState(int jobId, JobState state);

signals:
    void jobAdded(int jobId, const QString &label, const QString &command);

private slots:
    void onRowSelected();

private:
    QTableWidget *m_table = nullptr;
    QHash<int, int> m_rows;   // job id → row
};
