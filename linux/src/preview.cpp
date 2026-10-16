#include "preview.h"
#include <algorithm>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

Preview::Preview(QWidget* parent) : QWidget(parent) { setMouseTracking(true); setMinimumSize(480, 270); setFocusPolicy(Qt::StrongFocus); }

QRectF Preview::localRect(QRectF box) const {
    auto fit = FitInside(CanvasWidth, CanvasHeight, width(), height());
    double s = double(fit.w) / CanvasWidth;
    return {fit.x + box.x()*s, fit.y + box.y()*s, box.width()*s, box.height()*s};
}

void Preview::showFrame(const QImage& frame) { frame_ = frame; title_.clear(); body_.clear(); update(); }
void Preview::setMessage(const QString& title, const QString& body) { frame_ = {}; title_ = title; body_ = body; update(); }
void Preview::setSelection(QRectF logicalRect, bool handles) { selection_ = logicalRect; handles_ = handles; update(); }

void Preview::updateDestination() {
    auto fitted = FitInside(CanvasWidth, CanvasHeight, width(), height());
    destination_ = QRect(fitted.x, fitted.y, fitted.w, fitted.h);
}

QPointF Preview::logical(QPoint position) const {
    if (destination_.width() <= 0 || destination_.height() <= 0) return {};
    double x = (position.x() - destination_.x()) * double(CanvasWidth) / destination_.width();
    double y = (position.y() - destination_.y()) * double(CanvasHeight) / destination_.height();
    return {std::clamp(x, 0.0, double(CanvasWidth)), std::clamp(y, 0.0, double(CanvasHeight))};
}

void Preview::paintEvent(QPaintEvent*) {
    updateDestination();
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!frame_.isNull()) painter.drawImage(destination_, frame_);
    else {
        painter.setPen(Qt::white);
        painter.drawText(destination_, Qt::AlignCenter, title_ + (body_.isEmpty() ? QString() : "\n\n" + body_));
    }
    if (handles_ && !selection_.isNull()) {
        double sx = double(destination_.width()) / CanvasWidth, sy = double(destination_.height()) / CanvasHeight;
        QRectF rect(destination_.x() + selection_.x() * sx, destination_.y() + selection_.y() * sy, selection_.width() * sx, selection_.height() * sy);
        painter.setPen(QPen(QColor("#67e8f9"), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);
        painter.setBrush(Qt::white);
        for (auto point : {rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight()}) painter.drawRect(QRectF(point.x() - 4, point.y() - 4, 8, 8));
    }
    if (!hover_.isNull()) { painter.setPen(QPen(QColor("#f87171"), 2, Qt::DashLine)); painter.setBrush(Qt::NoBrush); painter.drawRect(localRect(hover_)); }
}

void Preview::resizeEvent(QResizeEvent*) { updateDestination(); }
void Preview::mousePressEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton && destination_.contains(event->pos())) { setFocus(); emit pressed(logical(event->pos()), event->position()); } }
void Preview::mouseMoveEvent(QMouseEvent* event) { emit moved(logical(event->pos())); }
void Preview::mouseReleaseEvent(QMouseEvent* event) { if (event->button() == Qt::LeftButton) emit released(logical(event->pos())); }
void Preview::mouseDoubleClickEvent(QMouseEvent* event) { if (destination_.contains(event->pos())) emit doubleClicked(logical(event->pos())); }
void Preview::wheelEvent(QWheelEvent* event) { emit wheeled(logical(event->position().toPoint()), event->angleDelta().y()); }
