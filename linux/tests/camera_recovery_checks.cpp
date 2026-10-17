#include "window.h"
#include "app_style.h"
#include <QApplication>
#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <iostream>
#include <cstdlib>

namespace {
void Check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
}

// Drive capture observations directly, without introducing a fake-camera flag
// into the shipped app or needing physical hardware in CI.
struct CameraRecoveryChecks {
    static void frame(MainWindow& window, int age = 0) {
        window.capture_.stop();
        window.capture_.latest_ = QImage(640, 480, QImage::Format_RGB32);
        window.capture_.latest_.fill(QColor("#45607a"));
        window.capture_.latestAt_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(age);
    }
    static void locked(MainWindow& window) {
        QApplication::processEvents();
        Check(window.stack_->currentWidget() == window.workspacePage_, "Workspace shell disappeared during recovery");
        Check(window.previewStack_->currentWidget() == window.recoveryPage_, "Recovery is not in the preview area");
        Check(!window.workspaceAllowed() && !window.libraryPanel_->isEnabled(), "Cards are accessible without video");
        Check(window.library_->isVisible() && !window.library_->isEnabled(), "Library should remain visible but disabled");
        Check(!window.annotateButton_->isEnabled() && !window.annotateBar_->isEnabled(), "Annotation controls are accessible without video");
        Check(window.camerasBox_->isVisible() && window.camerasBox_->isEnabled(), "Header camera selection is unavailable during recovery");
        Check(!window.preview_->isVisible(), "Recovery did not replace the preview");
        Check(window.recoveryRetry_->isVisible(), "Recovery has no retry action");
        Check(!window.preview_->hasFrame(), "Recovery retains a stale picture");
        Check(window.recoveryMessage_->height() >= window.recoveryMessage_->heightForWidth(window.recoveryMessage_->width()), "Recovery explanation is clipped");
        Check(window.recoveryTitle_->height() >= window.recoveryTitle_->heightForWidth(window.recoveryTitle_->width()), "Recovery heading is clipped");
    }
    static void run(QApplication& app) {
        Card card; card.id="saved"; card.name="Saved agenda"; card.html="<h1>Saved agenda</h1>";
        Annotation mark; mark.id=1; mark.kind=Tool::Text; mark.text="Keep this note"; mark.box={20,20,200,60};
        Store().setWorkspace({card}, {mark});
        MainWindow window;
        window.show(); app.processEvents(); window.timer_->stop();
        locked(window);
        const auto initial = window.recoveryTitle_->text();
        Check(initial == "We couldn't find your webcam" || initial == "Choose your webcam", "First-run camera guidance is missing");
        window.recoveryRetry_->click(); locked(window);
        Check(window.cards_.size()==1 && window.annotations_.size()==1, "Retry discarded saved work");

        window.wantedCamera_ = {"Test webcam", "test-camera", "/dev/test-camera"};
        window.cameraOpening_.start();
        window.showCameraRecovery("Opening your webcam…", "Test webcam");
        window.tick(); locked(window);
        Check(window.store_.preferences().cameraIdentity.isEmpty(), "Unopened webcam was remembered");
        QThread::msleep(5100); window.tick(); locked(window);
        Check(window.recoveryTitle_->text()=="No picture from your webcam", "Opening indefinitely did not offer recovery");

        frame(window); window.tick();
        Check(window.workspaceAllowed() && window.library_->isEnabled() && window.annotateButton_->isEnabled(), "Live picture did not unlock workspace");
        Check(window.previewStack_->currentWidget()==window.preview_, "Live picture did not replace preview recovery");
        Check(window.store_.preferences().cameraIdentity=="test-camera", "Working camera was not remembered");

        // Losing freshness locks even before the capture thread reports a fault.
        frame(window, 2000); window.tick(); locked(window);
        Check(window.recoveryTitle_->text()=="No picture from your webcam", "Frozen video has no recovery explanation");
        const auto count=window.annotations_.size();
        window.tool_=Tool::Eraser; window.pointerDown({30,30}); window.selectTool(Tool::Pen);
        Check(window.annotations_.size()==count && window.tool_==Tool::Eraser, "Locked input changed annotations");

        for (auto error : {CaptureError::Denied, CaptureError::Busy, CaptureError::Missing, CaptureError::Unreadable, CaptureError::NoFrames}) {
            frame(window); window.tick();
            window.capture_.error_=error; window.tick(); locked(window);
            const int messageHeight = window.recoveryMessage_->minimumHeight();
            for (int i=0; i<5; ++i) window.tick();
            Check(window.recoveryMessage_->minimumHeight()==messageHeight, "Repeated error updates keep growing the recovery panel");
            Check(!window.recoveryMessage_->text().isEmpty(), "Camera failure lacks recovery guidance");
            Check(window.cards_.size()==1 && window.annotations_.first().text==mark.text, "Camera failure changed the workspace");
        }
        Check(Store().cards().first().html==card.html && Store().annotations().first().text==mark.text, "Recovery did not preserve saved content");

        // A virtual-camera fault must leave a working physical preview usable.
        frame(window); window.outputWanted_=true; window.tick();
        for (int i=0; i<100 && window.output_.problem().isEmpty(); ++i) QThread::msleep(5);
        window.tick();
        Check(!window.output_.problem().isEmpty(), "Isolated test unexpectedly has a virtual camera");
        Check(window.workspaceAllowed() && window.library_->isVisible(), "Virtual-camera failure locked the working webcam");
        Check(window.status_->text()==window.output_.problem(), "Virtual-camera warning is hidden");
        window.refresh(); window.tick(); locked(window);
        Check(window.recoveryTitle_->text()=="Webcam not connected", "A previous opening deadline replaced missing-camera guidance");
        if (app.arguments().contains("--screenshots")) {
            window.capture_.error_=CaptureError::Denied; window.tick();
            Check(window.grab().save("/work/camera-access-blocked.png"), "Recovery screenshot failed");
            window.refresh();
            Check(window.grab().save("/work/camera-not-connected.png"), "Missing-camera screenshot failed");
        }
    }
};

int main(int argc, char** argv) {
    QApplication app(argc,argv); ApplyAppStyle(app);
    QTemporaryDir directory;
    Check(directory.isValid(), "Could not create isolated workspace");
    qputenv("XDG_DATA_HOME", directory.path().toUtf8()+"/data");
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8()+"/config");
    CameraRecoveryChecks::run(app);
    std::cout << "Camera startup, video loss, recovery, persistence, and output warning checks passed\n";
}
