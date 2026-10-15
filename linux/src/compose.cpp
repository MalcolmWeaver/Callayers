#include "compose.h"
#include "video_format.h"
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QtMath>
#include <unistd.h>

QImage ComposeFrame(const QImage& webcam, const QVector<Card>& cards, const QVector<Annotation>& annotations, quint64 hiddenTextId) {
    QImage frame(OutputWidth, OutputHeight, QImage::Format_ARGB32);
    frame.fill(Qt::black);
    QPainter painter(&frame);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!webcam.isNull()) {
        auto fitted = FitInside(webcam.width(), webcam.height(), OutputWidth, OutputHeight);
        painter.drawImage(QRect(fitted.x, fitted.y, fitted.w, fitted.h), webcam);
    }
    const double scale = double(OutputWidth) / CanvasWidth;
    QVector<const Card*> ordered;
    for (const auto& card : cards) if (card.visible) ordered.push_back(&card);
    std::sort(ordered.begin(), ordered.end(), [](const Card* a, const Card* b) { return a->layer < b->layer; });
    static QHash<QString, QImage> images;
    for (const Card* card : ordered) {
        QRectF rect(card->x * scale, card->y * scale, card->w * scale, card->h * scale);
        painter.setPen(QPen(QColor(255, 255, 255, 70), 2));
        painter.setBrush(QColor(19, 25, 40, 230));
        painter.drawRoundedRect(rect, 16, 16);
        QRectF inside = rect.adjusted(18, 16, -18, -16);
        if (!card->imagePath.isEmpty()) {
            if (!images.contains(card->imagePath)) images.insert(card->imagePath, QImage(card->imagePath));
            const QImage& image = images.value(card->imagePath);
            if (!image.isNull()) {
                auto fit = FitInside(image.width(), image.height(), int(inside.width()), int(inside.height()));
                painter.drawImage(QRectF(inside.x() + fit.x, inside.y() + fit.y, fit.w, fit.h), image);
            }
        } else if (!card->html.isEmpty()) {
            QTextDocument document;
            document.setDefaultStyleSheet("body { color: white; font-family: sans-serif; }");
            document.setHtml(card->html);
            document.setTextWidth(inside.width());
            painter.save();
            painter.setClipRect(inside);
            painter.translate(inside.topLeft());
            document.drawContents(&painter);
            painter.restore();
        } else {
            painter.setPen(Qt::white);
            painter.setFont(QFont("sans-serif", 18));
            painter.drawText(inside, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, card->name);
        }
    }
    for (const auto& mark : annotations) {
        if (mark.id == hiddenTextId) continue;
        QColor color(mark.color);
        if (!color.isValid()) color = QColor("#ffd166");
        QPen pen(color, InkWidths[std::clamp(mark.size, 0, 2)] * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        auto point = [&](QPointF p) { return QPointF(p.x() * scale, p.y() * scale); };
        QRectF box(mark.box.x() * scale, mark.box.y() * scale, mark.box.width() * scale, mark.box.height() * scale);
        if (mark.kind == Tool::Pen || mark.kind == Tool::Line || mark.kind == Tool::Arrow) {
            QPainterPath path;
            for (int i = 0; i < mark.points.size(); ++i) { if (i == 0) path.moveTo(point(mark.points[i])); else path.lineTo(point(mark.points[i])); }
            painter.drawPath(path);
            if (mark.kind == Tool::Arrow && mark.points.size() >= 2) {
                QPointF end = point(mark.points.back()), previous = point(mark.points[mark.points.size() - 2]);
                double angle = std::atan2(end.y() - previous.y(), end.x() - previous.x());
                QPointF a = end - QPointF(std::cos(angle - 0.45) * 18, std::sin(angle - 0.45) * 18);
                QPointF b = end - QPointF(std::cos(angle + 0.45) * 18, std::sin(angle + 0.45) * 18);
                painter.setBrush(color);
                painter.drawPolygon(QPolygonF{end, a, b});
            }
        } else if (mark.kind == Tool::Rectangle) painter.drawRect(box);
        else if (mark.kind == Tool::Ellipse) painter.drawEllipse(box);
        else if (mark.kind == Tool::Text) {
            painter.setFont(QFont("sans-serif", mark.font * scale / 1.4));
            painter.drawText(box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, mark.text);
        }
    }
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
