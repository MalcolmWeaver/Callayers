#include "window.h"
#include "app_style.h"
#include "video_format.h"
#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Callayers");
    QApplication::setApplicationVersion(CALLAYERS_VERSION);
    ApplyAppStyle(app);
    MainWindow window;
    if (app.arguments().contains("--self-check")) return 0;
    window.show();
    return app.exec();
}
