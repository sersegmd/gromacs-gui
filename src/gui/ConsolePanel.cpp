#include "ConsolePanel.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QPlainTextEdit>

ConsolePanel::ConsolePanel(QWidget *parent)
    : QDockWidget(tr("Console"), parent)
{
    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setMaximumBlockCount(10000);
    m_view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setWidget(m_view);
}

void ConsolePanel::appendInfo(const QString &message)
{
    appendColored(message, QStringLiteral("#1c6bb0"));
}

void ConsolePanel::appendWarning(const QString &message)
{
    appendColored(message, QStringLiteral("#b07d1c"));
}

void ConsolePanel::appendError(const QString &message)
{
    appendColored(message, QStringLiteral("#a82c2c"));
}

void ConsolePanel::appendRaw(const QString &chunk)
{
    m_view->appendPlainText(chunk);
}

void ConsolePanel::clear()
{
    m_view->clear();
}

void ConsolePanel::appendColored(const QString &message, const QString &color)
{
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_view->appendHtml(QStringLiteral(
        "<span style=\"color:gray;\">[%1]</span> "
        "<span style=\"color:%2;\">%3</span>")
        .arg(stamp, color, message.toHtmlEscaped()));
}
