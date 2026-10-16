#include "window.h"
#include "app_style.h"
#include "video_format.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include "scene_renderer.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Callayers");
    QApplication::setApplicationVersion(CALLAYERS_VERSION);
#ifndef CALLAYERS_DEVELOPMENT
    if (app.arguments().contains("--skip-sign-in") || app.arguments().contains("--offline-preview")) {
        qCritical("Local testing flags are unavailable in production builds.");
        return 2;
    }
#endif
    ApplyAppStyle(app);
    if (app.arguments().contains("--self-check")) {
        SceneRenderer renderer; QVector<Card> cards;
        QElapsedTimer timeout;timeout.start();
        while(timeout.elapsed()<5000) {
            if(renderer.render(cards,{}))return 0;
            if(!renderer.problem().isEmpty())return 1;
            QThread::msleep(10);
        }
        return 1;
    }
    MainWindow window;
    window.show();
    return app.exec();
}
