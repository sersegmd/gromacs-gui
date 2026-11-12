#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QTimer>

#include "app/MainWindow.h"
#include "gui/MdpEditorPanel.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setOrganizationName(QStringLiteral("MolecularStudio"));
    QApplication::setApplicationName(QStringLiteral("Molecular Studio"));
    QApplication::setApplicationVersion(QStringLiteral(MS_VERSION));

    // Fusion keeps a consistent look across Linux desktop environments.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Molecular Studio — a unified desktop workspace for GROMACS"));
    parser.addHelpOption();
    parser.addVersionOption();
    // Demo harness (used by CI): sets up a scenario before the shot.
    QCommandLineOption demoOption(
        QStringList() << QStringLiteral("demo"),
        QStringLiteral("Prepare demo scenario: main | wizard | analysis | editor | console | viewer"),
        QStringLiteral("mode"));
    QCommandLineOption projectOption(
        QStringList() << QStringLiteral("project"),
        QStringLiteral("Open the given project directory at startup."),
        QStringLiteral("dir"));
    // Renders a window to a PNG and exits; used by CI to smoke-test the UI.
    QCommandLineOption screenshotOption(
        QStringList() << QStringLiteral("screenshot"),
        QStringLiteral("Save a screenshot of the active window to <file> and exit."),
        QStringLiteral("file"));
    QCommandLineOption delayOption(
        QStringList() << QStringLiteral("shot-delay"),
        QStringLiteral("Milliseconds to wait before taking the screenshot (default 900)."),
        QStringLiteral("ms"), QStringLiteral("900"));
    parser.addOption(demoOption);
    parser.addOption(projectOption);
    parser.addOption(screenshotOption);
    parser.addOption(delayOption);
    parser.process(app);

    MainWindow window;
    window.show();

    if (parser.isSet(projectOption))
        window.loadProject(parser.value(projectOption));

    if (parser.isSet(screenshotOption)) {
        const QString file = parser.value(screenshotOption);
        const int delay = parser.value(delayOption).toInt();

        // Demo scenarios open their dialog/window before the shot.
        const QString mode = parser.value(demoOption);
        if (mode == QStringLiteral("wizard"))
            QTimer::singleShot(250, &window, [&window]() { window.openWizardForDemo(); });
        else if (mode == QStringLiteral("analysis"))
            QTimer::singleShot(250, &window, [&window]() { window.openAnalysisForDemo(); });
        else if (mode == QStringLiteral("editor")) {
            window.mdpEditor()->newFromTemplate();
            QTimer::singleShot(250, &window, [&window]() { window.mdpEditor()->validateNow(); });
        }
        else if (mode == QStringLiteral("console"))
            QTimer::singleShot(250, &window, [&window]() { window.runRealDemoCommand(); });
        else if (mode == QStringLiteral("viewer")) {
            // Load the newest .gro/.pdb of the project into the 3D viewer.
            QTimer::singleShot(250, &window, [&window]() { window.loadNewestStructureForDemo(); });
        }

        QTimer::singleShot(delay + 250, &window, [&window, file]() {
            QWidget *target = QApplication::activeWindow();
            if (!target)
                target = &window;
            target->grab().save(file);
            QApplication::exit(0);
        });
        return app.exec();
    }

    return app.exec();
}
