#include "ProjectPanel.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QStyle>
#include <QTreeWidget>

namespace {

// GROMACS file extension → logical group in the project tree.
struct FileCategory {
    const char *suffix;
    const char *group;
};

constexpr FileCategory kCategories[] = {
    {".pdb",  "Structures"},
    {".gro",  "Structures"},
    {".top",  "Topology"},
    {".itp",  "Topology"},
    {".mdp",  "Parameters"},
    {".tpr",  "Run Inputs"},
    {".xtc",  "Trajectories"},
    {".trr",  "Trajectories"},
    {".edr",  "Energies"},
    {".log",  "Logs"},
    {".cpt",  "Checkpoints"},
    {".ndx",  "Index Files"},
};

QString groupForFile(const QFileInfo &info)
{
    const QString name = info.fileName().toLower();
    for (const FileCategory &c : kCategories) {
        if (name.endsWith(c.suffix))
            return QLatin1String(c.group);
    }
    return QLatin1String("Other Files");
}

} // namespace

ProjectPanel::ProjectPanel(QWidget *parent)
    : QDockWidget(tr("Project"), parent)
{
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabel(tr("Files"));
    m_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    setWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                const QString path = item->data(0, Qt::UserRole).toString();
                if (!path.isEmpty())
                    emit fileSelected(path);
            });
}

void ProjectPanel::loadProject(const QString &projectDir)
{
    m_projectDir = projectDir;
    refresh();
}

void ProjectPanel::refresh()
{
    if (m_projectDir.isEmpty())
        return;
    m_tree->clear();
    scanDirectory(m_projectDir);
}

void ProjectPanel::scanDirectory(const QString &dir)
{
    QHash<QString, QTreeWidgetItem *> groups;

    QDir projectDir(dir);
    projectDir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                         | QDir::NoSymLinks);
    projectDir.setSorting(QDir::DirsFirst | QDir::Name);

    const QFileInfoList entries = projectDir.entryInfoList();
    for (const QFileInfo &info : entries) {
        if (info.isDir()) {
            // One level of subdirectories is shown as folders; deeper
            // scanning arrives with the core engine.
            QTreeWidgetItem *folder =
                new QTreeWidgetItem(m_tree, {info.fileName()});
            folder->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            folder->setDisabled(false);
            continue;
        }
        const QString group = groupForFile(info);
        if (!groups.contains(group))
            groups.insert(group, groupItem(group));
        addFileEntry(groups.value(group), info.absoluteFilePath());
    }
}

QTreeWidgetItem *ProjectPanel::groupItem(const QString &title)
{
    QTreeWidgetItem *item = new QTreeWidgetItem(m_tree, {title});
    item->setFirstColumnSpanned(true);
    QFont bold = item->font(0);
    bold.setBold(true);
    item->setFont(0, bold);
    return item;
}

void ProjectPanel::addFileEntry(QTreeWidgetItem *group, const QString &path)
{
    QTreeWidgetItem *item = new QTreeWidgetItem(group, {QFileInfo(path).fileName()});
    item->setData(0, Qt::UserRole, path);
    item->setToolTip(0, path);
}
