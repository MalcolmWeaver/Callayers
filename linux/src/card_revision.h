#pragma once
#include "card_validation.h"
#include <QDateTime>
inline bool ApplyCardRevision(QVector<Card>& cards, const QString& id, quint64 version, const QJsonObject& result) {
    auto target=std::find_if(cards.begin(),cards.end(),[&](const Card& c){return c.id==id && c.contentVersion==version;});
    if(target==cards.end()) return false;
    auto next=*target;
    next.previousName=target->name; next.previousHtml=target->html;
    next.previousW=target->contentW; next.previousH=target->contentH; next.hasPrevious=true;
    next.html=result["html"].toString(); next.contentW=result["width"].toDouble(); next.contentH=result["height"].toDouble();
    next.state={}; ++next.contentVersion;
    if (!ValidCardContent(next)) return false;
    FitCardContent(next); next.edited=QDateTime::currentMSecsSinceEpoch();
    *target=next; return true;
}
