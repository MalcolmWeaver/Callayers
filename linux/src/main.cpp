#include "window.h"
#include "video_format.h"
#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Callayers");
    QApplication::setApplicationVersion(CALLAYERS_VERSION);
    app.setStyleSheet("QWidget { background:#1d1d21; color:#f4f3fa; } QLineEdit, QPlainTextEdit, QListWidget, QComboBox { background:#151519; border:1px solid #3c3c44; border-radius:6px; padding:6px; } QPushButton { background:#2e2e34; border:1px solid #3c3c44; border-radius:6px; padding:8px 12px; } QPushButton:hover { background:#3a3a42; } QMenuBar, QMenu { background:#1d1d21; }");
    MainWindow window;
    if (app.arguments().contains("--self-check")) return 0;
    window.show();
    return app.exec();
}
