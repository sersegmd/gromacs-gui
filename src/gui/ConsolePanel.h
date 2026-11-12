#pragma once

#include <QDockWidget>

class QPlainTextEdit;

/**
 * @brief Bottom dock: unified log for GROMACS stdout/stderr and app messages.
 */
class ConsolePanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit ConsolePanel(QWidget *parent = nullptr);

public slots:
    void appendInfo(const QString &message);
    void appendWarning(const QString &message);
    void appendError(const QString &message);
    void appendRaw(const QString &chunk);   // verbatim process output
    void clear();

private:
    void appendColored(const QString &message, const QString &color);

    QPlainTextEdit *m_view = nullptr;
};
