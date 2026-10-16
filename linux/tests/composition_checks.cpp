#include "compose.h"
#include "video_format.h"
#include <QCoreApplication>
#include <QPainter>
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
bool Near(QRgb actual, int red, int green, int blue) {
    return qAlpha(actual) == 255 && std::abs(qRed(actual) - red) <= 1 &&
           std::abs(qGreen(actual) - green) <= 1 && std::abs(qBlue(actual) - blue) <= 1;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QImage camera(1280, 720, QImage::Format_RGB32);
    camera.fill(Qt::blue);
    QImage overlay(OutputWidth, OutputHeight, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    {
        QPainter painter(&overlay);
        painter.fillRect(100, 100, 100, 100, QColor(255, 0, 0, 128));
        painter.fillRect(300, 100, 100, 100, Qt::green);
    }
    const auto frame = ComposeFrame(camera, overlay);
    Check(frame.size() == QSize(1920, 1080), "Composition must retain 1080p output");
    Check(Near(frame.pixel(50, 50), 0, 0, 255), "Transparent overlay must reveal the webcam");
    Check(Near(frame.pixel(150, 150), 128, 0, 127), "Partial overlay transparency must blend with the webcam");
    Check(Near(frame.pixel(350, 150), 0, 255, 0), "Opaque overlay must cover the webcam");

    QImage squareCamera(640, 480, QImage::Format_RGB32);
    squareCamera.fill(Qt::white);
    const auto bars = ComposeFrame(squareCamera, {});
    Check(bars.pixel(239, 540) == qRgb(0, 0, 0) && bars.pixel(240, 540) == qRgb(255, 255, 255) &&
          bars.pixel(1679, 540) == qRgb(255, 255, 255) && bars.pixel(1680, 540) == qRgb(0, 0, 0),
          "Non-widescreen cameras must retain their aspect ratio and opaque black bars");
    const auto offline = ComposeFrame({}, overlay);
    Check(Near(offline.pixel(50, 50), 0, 0, 0) && Near(offline.pixel(150, 150), 128, 0, 0),
          "Preview without a webcam must blend the overlay over black");

    std::vector<uint8_t> direct, converted;
    BgraToYuyv(frame.constBits(), frame.width(), frame.height(), direct);
    const auto argb = frame.convertToFormat(QImage::Format_ARGB32);
    BgraToYuyv(argb.constBits(), argb.width(), argb.height(), converted);
    Check(direct == converted && direct.size() == size_t(OutputWidth) * OutputHeight * 2,
          "Opaque composition must retain the virtual camera's color byte layout");
    std::cout << "Opaque composition, overlay blending, aspect ratio, and output checks passed\n";
}
