#include "window.h"
#include "app_style.h"
#include "compose.h"
#include "scene_renderer.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QThread>
#include <cstdlib>
#include <iostream>
#include <cmath>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
bool WaitForFrame(Preview* preview) {
    QElapsedTimer timer;
    timer.start();
    while (!preview->hasFrame() && timer.elapsed() < 6000) {
        QApplication::processEvents();
        QThread::msleep(10);
    }
    return preview->hasFrame();
}
void Screenshot(QWidget& window, const QString& directory, const QString& name) {
    if (directory.isEmpty()) return;
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 800) {
        QApplication::processEvents();
        QThread::msleep(10);
    }
    Check(QDir().mkpath(directory), "Could not create screenshot directory");
    Check(window.grab().save(QDir(directory).filePath(name)), "Could not save screenshot");
}
Card SampleCard() {
    Card card;
    card.id = "local-card-verification";
    card.name = "Team update — test card";
    card.x = 80;
    card.y = 70;
    card.html = R"(<html><body style="color:#ffffff; font-family:sans-serif;">
<p style="color:#a39bfa; font-size:20px;">CALLAYERS / SAMPLE AGENDA</p>
<h1 style="color:#ffffff; font-size:48px;">Team update</h1>
<p style="color:#ffffff; font-size:24px;">01 &nbsp; Welcome</p>
<p style="color:#ffffff; font-size:24px;">02 &nbsp; Product demo</p>
<p style="color:#ffffff; font-size:24px;">03 &nbsp; Questions</p>
</body></html>)";
    return card;
}
void Drag(Preview& preview, QPointF from, QPointF to) {
    auto box = FitInside(CanvasWidth, CanvasHeight, preview.width(), preview.height());
    auto local = [&](QPointF p) {
        return QPointF(box.x + p.x() * box.w / CanvasWidth, box.y + p.y() * box.h / CanvasHeight);
    };
    auto send = [&](QEvent::Type type, QPointF point, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const auto position = local(point);
        QMouseEvent event(type, position, QPointF(preview.mapToGlobal(position.toPoint())), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(&preview, &event);
        QApplication::processEvents();
    };
    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}
QImage CardFrame(const Card& card) {
    SceneRenderer renderer; QVector<Card> cards{card};
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<5000) {
        if(renderer.render(cards,{}))return ComposeFrame({},renderer.image());
        QApplication::processEvents();QThread::msleep(10);
    }
    Check(false,"Card renderer did not start");return {};
}
int WhitePixels(const QImage& image) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qRed(row[x]) > 225 && qGreen(row[x]) > 225 && qBlue(row[x]) > 225) ++count;
    }
    return count;
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ApplyAppStyle(app);
    QApplication::setApplicationName("CallayersWorkspaceTest");
    QTemporaryDir directory;
    Check(directory.isValid(), "Could not create isolated test workspace");
    qputenv("XDG_DATA_HOME", directory.path().toUtf8() + "/data");
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8() + "/config");
    const bool hardware = app.arguments().contains("--hardware");
    const bool testCard = app.arguments().contains("--test-card");
    int cardFailures = 0;
    Card finalCard;
    if (testCard) Store().setCards({SampleCard()});
    const int screenshotsArg = app.arguments().indexOf("--screenshots");
    const QString screenshots = screenshotsArg >= 0 ? app.arguments().value(screenshotsArg + 1) : QString();
    Check(screenshotsArg < 0 || !screenshots.isEmpty(), "--screenshots requires a directory");
    {
        MainWindow window;
        window.show();
        QApplication::processEvents();
        auto stack = window.findChild<QStackedWidget*>();
        auto selector = window.findChild<QComboBox*>("cameraSelector");
        auto retry = window.findChild<QPushButton*>("retryCamera");
        auto status = window.findChild<QLabel*>("cameraStatus");
        auto preview = window.findChild<Preview*>();
        bool expectsBypass = false;
#ifdef CALLAYERS_DEVELOPMENT
        expectsBypass = app.arguments().contains("--skip-sign-in");
#endif
        if (!expectsBypass) {
            Check(stack && stack->currentIndex() == 0, "Default development startup bypassed sign-in");
            QPushButton* signIn = nullptr;
            for (auto button : window.findChildren<QPushButton*>())
                if (button->text() == "Sign in with Google") signIn = button;
            Check(signIn && signIn->isVisible(), "Google sign-in button is not visible");
            Check(preview && !preview->hasFrame(), "Workspace rendered before authentication");
            Check(selector && !selector->isVisible(), "Camera selector exposed before authentication");
            std::cout << "Default startup requires sign-in, including offline preview\n";
            return 0;
        }

        Check(stack && stack->currentIndex() == 1, "Explicit test flag did not bypass sign-in");
        Check(selector && retry && status && preview, "Workspace controls are missing");
        Check(selector->currentIndex() == -1, "Fresh workspace silently selected a camera");
        Screenshot(window, screenshots, "01-workspace-without-login.png");
        retry->click();
        QApplication::processEvents();
        Check(stack->currentIndex() == 1 && selector->currentIndex() == -1,
              "Refresh locked the workspace or silently selected a camera");
        if (hardware) {
            Check(selector->count() > 0, "No hardware camera was enumerated");
            QKeyEvent choose(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
            QApplication::sendEvent(selector, &choose);
            if (!WaitForFrame(preview)) {
                std::cerr << "Camera status: " << status->text().toStdString() << '\n';
                return 1;
            }
            std::cout << "Live preview: " << selector->currentText().toStdString() << '\n';
            if (app.arguments().contains("--measure-fps")) {
                // Keep the real app pipeline running while a separate V4L2
                // reader measures delivery from the virtual camera.
                std::cout << "Virtual camera measurement ready (30 seconds)" << std::endl;
                QElapsedTimer measurement;
                measurement.start();
                bool minimized = false, restored = false, consumerSeen = false;
                while (measurement.elapsed() < 30000) {
                    QApplication::processEvents();
                    consumerSeen |= status->text().startsWith("Sending video to ");
                    if (!minimized && measurement.elapsed() >= 12000) {
                        Check(consumerSeen, "No virtual-camera reader detected during measurement");
                        window.showMinimized();
                        minimized = true;
                        std::cout << "Measuring while minimized" << std::endl;
                    }
                    if (!restored && measurement.elapsed() >= 22000) {
                        window.showNormal();
                        restored = true;
                        std::cout << "Measurement window restored" << std::endl;
                    }
                    QThread::msleep(1);
                }
            }
            Screenshot(window, screenshots, "02-live-webcam.png");
            retry->click();
            Check(WaitForFrame(preview), "Retry did not restore the live preview");
            Screenshot(window, screenshots, "03-after-retry.png");
            std::cout << "Retry: live preview restored\nOutput status: " << status->text().toStdString() << '\n';
            if (testCard) {
                auto library = window.findChild<QListWidget*>();
                Check(library && library->count() == 1 && library->item(0)->text() == SampleCard().name,
                      "Sample card did not load in the library");
                const auto original = Store().cards().first();
                const auto rendered = CardFrame(original);
                const int originalTextPixels = WhitePixels(rendered);
                Check(originalTextPixels > 100, "Sample card text did not render");
                Screenshot(window, screenshots, "05-test-card-visible.png");
                library->item(0)->setCheckState(Qt::Unchecked);
                const auto hidden = Store().cards().first();
                Check(!hidden.visible && CardFrame(hidden) == ComposeFrame({}, {}),
                      "Hiding the card did not remove it from the composition");
                Screenshot(window, screenshots, "06-test-card-hidden.png");
                library->item(0)->setCheckState(Qt::Checked);
                Check(Store().cards().first().visible, "Showing the card did not persist");
                const QPointF start(original.x + 4, original.y + 40);
                Drag(*preview, start, start + QPointF(160, 90));
                const auto moved = Store().cards().first();
                Check(std::abs(moved.x - original.x - 160) < 3 && std::abs(moved.y - original.y - 90) < 3,
                      "Dragging did not move and save the card");
                Screenshot(window, screenshots, "07-test-card-moved.png");
                const QPointF corner(moved.x + moved.w - 2, moved.y + moved.h - 2);
                Drag(*preview, corner, corner + QPointF(122, 69));
                finalCard = Store().cards().first();
                Check(finalCard.w > original.w + 100 && std::abs(finalCard.w / finalCard.h - original.w / original.h) < 0.01,
                      "Resizing did not enlarge the card box proportionally");
                Screenshot(window, screenshots, "08-test-card-resized.png");
                const int resizedTextPixels = WhitePixels(CardFrame(finalCard));
                const double areaFactor = std::pow(finalCard.w / original.w, 2);
                if (resizedTextPixels < originalTextPixels * areaFactor * 0.8) {
                    ++cardFailures;
                    std::cout << "FAIL: card box resized by " << finalCard.w / original.w
                              << "x, but text coverage changed by " << double(resizedTextPixels) / originalTextPixels
                              << "x (expected approximately " << areaFactor << "x). Text does not scale with the card.\n";
                }
                std::cout << "PASS: static HTML text, hide/show, drag, box resize, saved placement\n";
            }
        }
    }
    if (hardware) {
        MainWindow reopened;
        reopened.show();
        auto preview = reopened.findChild<Preview*>();
        Check(preview && WaitForFrame(preview), "Saved webcam did not start after reopening");
        Screenshot(reopened, screenshots, "04-restored-webcam.png");
        std::cout << "Reopen: saved webcam restored automatically\n";
        if (testCard) {
            const auto restored = Store().cards().first();
            auto library = reopened.findChild<QListWidget*>();
            Check(library && library->count() == 1 && library->item(0)->checkState() == Qt::Checked &&
                  restored.html == finalCard.html && restored.x == finalCard.x && restored.y == finalCard.y &&
                  restored.w == finalCard.w && restored.h == finalCard.h, "Card did not restore after reopening");
            Screenshot(reopened, screenshots, "09-test-card-restored.png");
            std::cout << "PASS: card content, visibility, position, and size restored after reopening\n";
        }
    }
    std::cout << (cardFailures ? "Development workspace checks completed with card failures\n" : "Development workspace checks passed\n");
    return cardFailures ? 2 : 0;
}
