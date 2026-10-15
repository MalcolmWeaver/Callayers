#pragma once
#include "model.h"
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTransform>
#include <algorithm>

inline QRectF AnnotationBounds(const Annotation& mark) {
    if (mark.kind != Tool::Pen && mark.kind != Tool::Line && mark.kind != Tool::Arrow) return mark.box;
    if (mark.points.isEmpty()) return {};
    double left=mark.points.first().x(), right=left, top=mark.points.first().y(), bottom=top;
    for (auto p : mark.points) { left=std::min(left,p.x()); right=std::max(right,p.x()); top=std::min(top,p.y()); bottom=std::max(bottom,p.y()); }
    return QRectF(left, top, std::max(1.0,right-left), std::max(1.0,bottom-top));
}
inline bool AnnotationHit(const Annotation& mark, QPointF point) {
    QPainterPath path;
    if (mark.kind == Tool::Text || mark.kind == Tool::Rectangle) return mark.box.adjusted(-5,-5,5,5).contains(point);
    if (mark.kind == Tool::Ellipse) { path.addEllipse(mark.box.adjusted(-5,-5,5,5)); return path.contains(point); }
    if (mark.points.isEmpty()) return false;
    path.moveTo(mark.points.first());
    for (int i=1;i<mark.points.size();++i) path.lineTo(mark.points[i]);
    QPainterPathStroker stroke; stroke.setWidth(std::max(12.0, InkWidths[std::clamp(mark.size,0,2)]+8));
    stroke.setCapStyle(Qt::RoundCap); stroke.setJoinStyle(Qt::RoundJoin);
    return stroke.createStroke(path).contains(point) || QLineF(mark.points.first(),point).length() < 6;
}
inline int CornerAt(QRectF box, QPointF point) {
    const QPointF corners[]{box.topLeft(),box.topRight(),box.bottomLeft(),box.bottomRight()};
    for (int i=0;i<4;++i) if (QLineF(corners[i],point).length() <= 12) return i;
    return -1;
}
inline QRectF ResizedBox(QRectF original, QPointF point, int corner, bool proportional) {
    const QPointF corners[]{original.topLeft(),original.topRight(),original.bottomLeft(),original.bottomRight()};
    const QPointF anchor = corners[3-corner];
    const double sx = corner%2 ? 1 : -1, sy = corner>=2 ? 1 : -1;
    double width=std::max(8.0,(point.x()-anchor.x())*sx), height=std::max(8.0,(point.y()-anchor.y())*sy);
    const double maxW=sx>0?1280-anchor.x():anchor.x(), maxH=sy>0?720-anchor.y():anchor.y();
    if (proportional) {
        double scale=std::min({width/std::max(1.0,original.width()),maxW/std::max(1.0,original.width()),maxH/std::max(1.0,original.height())});
        width=original.width()*scale; height=original.height()*scale;
    } else { width=std::min(width,maxW); height=std::min(height,maxH); }
    return QRectF(anchor, anchor+QPointF(sx*width,sy*height)).normalized();
}
inline Annotation TransformAnnotation(const Annotation& original, QRectF target) {
    auto mark=original;
    auto bounds=AnnotationBounds(original);
    double sx=target.width()/std::max(1.0,bounds.width()), sy=target.height()/std::max(1.0,bounds.height());
    for (auto& p:mark.points) p=QPointF(target.x()+(p.x()-bounds.x())*sx,target.y()+(p.y()-bounds.y())*sy);
    mark.box=target;
    if (mark.kind==Tool::Text) mark.font=std::clamp(original.font*sx,8.0,600.0);
    return mark;
}
