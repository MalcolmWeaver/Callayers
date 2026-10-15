#pragma once
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QtGlobal>

enum class Tool { Select, Pen, Rectangle, Ellipse, Line, Arrow, Text, Eraser };

struct Card {
    QString id, name, html, imagePath;
    double x = 48, y = 48, w = 480, h = 270, contentW = 480, contentH = 270;
    bool visible = true;
    int layer = 0;
    qint64 edited = 0;
    QString previousName, previousHtml;
    double previousW = 0, previousH = 0;
    bool hasPrevious = false;
};

struct Annotation {
    quint64 id = 0;
    Tool kind = Tool::Pen;
    QString color = "#ffd166";
    int size = 1;
    double font = 36;
    QRectF box;
    QVector<QPointF> points;
    QString text;
};

inline constexpr double InkWidths[] = {3, 6, 10};
inline constexpr double TextSizes[] = {24, 36, 54};
