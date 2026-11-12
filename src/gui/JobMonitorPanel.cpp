#include "JobMonitorPanel.h"

#include <QColor>
#include <QDateTime>
#include <QHeaderView>
#include <QStringList>
#include <QTableWidget>

namespace {

QString stateToString(JobMonitorPanel::JobState state)
{
    switch (state) {
    case JobMonitorPanel::JobState::Queued:    return QObject::tr("Queued");
    case JobMonitorPanel::JobState::Running:   return QObject::tr("Running");
    case JobMonitorPanel::JobState::Completed: return QObject::tr("Completed");
    case JobMonitorPanel::JobState::Failed:    return QObject::tr("Failed");
    }
    return {};
}

QColor stateColor(JobMonitorPanel::JobState state)
{
    switch (state) {
    case JobMonitorPanel::JobState::Queued:    return QColor(0x8a, 0x8a, 0x8a);
    case JobMonitorPanel::JobState::Running:   return QColor(0x1c, 0x6b, 0xb0);
    case JobMonitorPanel::JobState::Completed: return QColor(0x2d, 0x86, 0x2d);
    case JobMonitorPanel::JobState::Failed:    return QColor(0xa8, 0x2c, 0x2c);
    }
    return {};
}

} // namespace

JobMonitorPanel::JobMonitorPanel(QWidget *parent)
    : QDockWidget(tr("Jobs"), parent)
{
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({tr("#"), tr("Task"), tr("State"), tr("Started")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    setWidget(m_table);
}

int JobMonitorPanel::addJob(int jobId, const QString &label, const QString &command)
{
    Q_UNUSED(command);   // implementation detail — only the task name is shown
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_rows.insert(jobId, row);

    m_table->setItem(row, 0, new QTableWidgetItem(QString::number(jobId)));
    m_table->setItem(row, 1, new QTableWidgetItem(label));
    setJobState(jobId, JobState::Running);
    m_table->setItem(row, 3,
                     new QTableWidgetItem(QDateTime::currentDateTime().toString("hh:mm:ss")));
    emit jobAdded(jobId, label, command);
    return row;
}

void JobMonitorPanel::setJobState(int jobId, JobState state)
{
    if (!m_rows.contains(jobId))
        return;
    const int row = m_rows.value(jobId);
    QTableWidgetItem *item = new QTableWidgetItem(stateToString(state));
    item->setForeground(stateColor(state));
    m_table->setItem(row, 3, item);
}

void JobMonitorPanel::onRowSelected()
{
}
