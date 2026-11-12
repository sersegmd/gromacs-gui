#pragma once

#include <QDockWidget>

class CoreBridge;
class QPlainTextEdit;
class QComboBox;
class QLabel;

/**
 * @brief Right dock: .mdp parameter editor with the built-in workflow
 * templates and a live linter (driven by the Rust core validator).
 *
 * The linter flags missing/invalid keys, large timesteps, thermostat /
 * barostat inconsistencies and unknown parameters before `gmx grompp` fails.
 */
class MdpEditorPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit MdpEditorPanel(QWidget *parent = nullptr);

    //! Injected by MainWindow (the core is created before the docks).
    void setCore(CoreBridge *core) { m_core = core; }

public slots:
    void openFile(const QString &path);
    void newFromTemplate();
    void saveCurrent();
    void validateNow();

signals:
    void fileSaved(const QString &path);

private:
    void updateValidation();

    CoreBridge *m_core = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QComboBox *m_templateBox = nullptr;
    QLabel *m_status = nullptr;
    QString m_currentPath;
};
