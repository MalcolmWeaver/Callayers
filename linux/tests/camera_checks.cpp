#include "camera_selector.h"
#include <QApplication>
#include <QKeyEvent>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
void WaitForCapture(Capture& capture) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (capture.running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(!capture.running(), "Capture did not finish opening an absent camera");
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Opt-in hardware measurement: count newly decoded images, not repeated
    // reads of the latest frame or the device's advertised frame rate.
    if (app.arguments().contains("--measure-fps")) {
        const auto cameras = EnumerateCameras();
        Check(!cameras.isEmpty(), "No hardware camera available");
        Capture capture;
        capture.start(cameras.first().identity);
        const auto warmup = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < warmup)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        auto frame = capture.latest();
        Check(!frame.isNull(), "Camera did not deliver a frame");
        auto key = frame.cacheKey();
        const auto started = std::chrono::steady_clock::now();
        int frames = 0;
        while (std::chrono::steady_clock::now() - started < std::chrono::seconds(10)) {
            frame = capture.latest();
            if (!frame.isNull() && frame.cacheKey() != key) { ++frames; key = frame.cacheKey(); }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double fps = frames / seconds;
        std::cout << cameras.first().name.toStdString() << ": " << frame.width() << 'x' << frame.height()
                  << ", " << frames << " fresh decoded frames / " << seconds << " seconds = " << fps << " fps\n";
        Check(capture.error() == CaptureError::None && fps >= 27, "Camera did not sustain approximately 30 fps");
        return 0;
    }
    QComboBox selector;
    const CameraChoice first{"Built-in webcam", "camera-a", "/dev/video0"};
    const CameraChoice second{"USB webcam", "camera-b", "/dev/video2"};
    int activations = 0, activatedIndex = -1;
    QObject::connect(&selector, &QComboBox::activated, [&](int index) {
        ++activations;
        activatedIndex = index;
    });

    Check(PopulateCameraSelector(selector, {first}, {}, {}) == -1, "First launch must require a choice");
    Check(selector.currentIndex() == -1, "Only camera must not appear selected before opening");
    Check(activations == 0, "Populating cameras must not open one");
    // Use an actual widget input event: choosing the sole entry must activate it.
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(&selector, &down);
    Check(activations == 1 && activatedIndex == 0, "First camera choice must trigger capture");

    Check(PopulateCameraSelector(selector, {second, first}, first.identity, first.name) == 1,
          "Restore must follow camera identity after device order changes");
    Check(selector.currentIndex() == 1 && activations == 1, "Restore must not start capture twice");
    Check(PopulateCameraSelector(selector, {second}, first.identity, first.name) == -1,
          "An absent saved camera must not switch to another webcam");
    Check(selector.currentIndex() == -1 && selector.placeholderText().contains(first.name),
          "The unavailable saved camera must remain identifiable");
    Check(PopulateCameraSelector(selector, {}, first.identity, first.name) == -1 && selector.count() == 0,
          "No-camera refresh must remain usable");
    Check(PopulateCameraSelector(selector, {first}, first.identity, first.name) == 0,
          "Retry must find the reconnected saved webcam");
    Check(activations == 1, "Rescanning must not produce spurious camera activations");

    Capture capture;
    capture.start("callayers-test-nonexistent-camera");
    WaitForCapture(capture);
    Check(capture.error() == CaptureError::Missing && capture.latest().isNull(),
          "Missing camera must produce a stable error without a stale picture");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Check(capture.error() == CaptureError::Missing && !capture.running(), "Failure must remain until retry");
    capture.start("callayers-test-nonexistent-camera");
    WaitForCapture(capture);
    Check(capture.error() == CaptureError::Missing, "Explicit retry must finish with the current failure");
    capture.stop();
    Check(!capture.running() && capture.error() == CaptureError::None && capture.latest().isNull(),
          "Stopping capture must clear the previous attempt");
    std::cout << "camera selection and recovery checks passed\n";
}
