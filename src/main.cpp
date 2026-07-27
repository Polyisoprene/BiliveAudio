#include <QApplication>
#include "utils/Logger.h"
#include "utils/Settings.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("BiliveAudio");
    app.setOrganizationName("BiliveAudio");
    app.setQuitOnLastWindowClosed(false);

    Logger::init();
    LOG_INFO("=== BiliveAudio starting ===");

    MainWindow window;
    auto &settings = Settings::instance();

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
