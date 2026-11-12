#pragma once

#include <QDockWidget>

class QTreeWidget;
class QTreeWidgetItem;

/**
 * @brief Left dock: project tree.
 *
 * Organises the current project directory into the groups a GROMACS
 * workflow actually produces (topology, parameters, structures,
 * trajectories, energies) instead of dumping raw files at the user.
 * The project itself is untouched — this is only a view.
 */
class ProjectPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit ProjectPanel(QWidget *parent = nullptr);

    void loadProject(const QString &projectDir);
    //! Re-scans the current project directory (after jobs produce files).
    void refresh();
    QString projectDir() const { return m_projectDir; }

signals:
    void fileSelected(const QString &path);

private:
    void scanDirectory(const QString &dir);
    void addFileEntry(QTreeWidgetItem *group, const QString &path);
    QTreeWidgetItem *groupItem(const QString &title);

    QTreeWidget *m_tree = nullptr;
    QString m_projectDir;
};
