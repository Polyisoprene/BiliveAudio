#include <clocale>
#include <QApplication>
#include "utils/Logger.h"
#include "utils/Settings.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    app.setApplicationName("BiliveAudio");
    app.setOrganizationName("BiliveAudio");
    app.setQuitOnLastWindowClosed(false);

    auto &settings = Settings::instance();
    Logger::init(settings.logDir().toStdString(), settings.logRetentionDays());
    LOG_INFO("=== BiliveAudio starting ===");

    MainWindow window;

    // Restore window geometry
    int wx = settings.windowX();
    int wy = settings.windowY();
    int ww = settings.windowWidth();
    int wh = settings.windowHeight();
    if (wx >= 0 && wy >= 0)
        window.setGeometry(wx, wy, ww, wh);

    window.show();
    LOG_INFO("MainWindow displayed");

    int ret = app.exec();
    LOG_INFO("=== BiliveAudio exiting ===");
    return ret;
}
