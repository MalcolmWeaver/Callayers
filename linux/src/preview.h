#pragma once
#include "video_format.h"
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>

class Preview : public QWidget {
    Q_OBJECT
public:
    explicit Preview(QWidget* parent = nullptr);
    void showFrame(const QImage& frame);
    void setMessage(const QString& title, const QString& body);
    void setSelection(QRectF logical, bool handles);
    QRectF selection() const { return selection_; }
    bool hasFrame() const { return !frame_.isNull(); }
signals:
    void pressed(QPointF logical, QPointF local);
    void moved(QPointF logical);
    void released(QPointF logical);
    void doubleClicked(QPointF logical);
    void wheeled(QPointF logical, int delta);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    QPointF logical(QPoint position) const;
    void updateDestination();
    QImage frame_;
    QString title_, body_;
    QRect destination_;
    QRectF selection_;
    bool handles_ = false;
};
