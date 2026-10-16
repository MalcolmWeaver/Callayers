#include "compose.h"
#include "video_format.h"
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <unistd.h>

QImage ComposeFrame(const QImage& webcam, const QImage& overlay) {
    // The finished picture is opaque; only the overlay needs an alpha channel.
    QImage frame(OutputWidth,OutputHeight,QImage::Format_RGB32);
    frame.fill(Qt::black);
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!webcam.isNull()) {
        const auto fit=FitInside(webcam.width(),webcam.height(),OutputWidth,OutputHeight);
        painter.drawImage(QRect(fit.x,fit.y,fit.w,fit.h),webcam);
    }
    if (!overlay.isNull()) painter.drawImage(0,0,overlay);
    return frame;
}

QString ConsumerName() {
    QDir processes("/proc");
    QString self = QString::number(getpid());
    for (const auto& pid : processes.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (pid == self || !pid[0].isDigit()) continue;
        QDir fds("/proc/" + pid + "/fd");
        for (const auto& fd : fds.entryList()) {
            if (QFile::symLinkTarget(fds.filePath(fd)) == OutputDevice) {
                QFile command("/proc/" + pid + "/comm");
                if (command.open(QIODevice::ReadOnly)) {
                    QString name = QString::fromUtf8(command.readAll()).trimmed();
                    if (!name.isEmpty() && name != "callayers") return name;
                }
                return "unknown";
            }
        }
    }
    return {};
}
