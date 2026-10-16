#pragma once
#include "model.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>

inline bool ValidStateValue(const QJsonValue& value, int depth = 0) {
    if (depth > 4) return false;
    if (value.isDouble()) return std::isfinite(value.toDouble());
    if (value.isString()) return value.toString().size() <= 4096;
    if (value.isArray()) {
        auto array = value.toArray();
        if (array.size() > 128) return false;
        for (const auto& child : array) if (!ValidStateValue(child, depth + 1)) return false;
    }
    if (value.isObject()) {
        auto object = value.toObject();
        if (object.size() > 64) return false;
        for (auto it = object.begin(); it != object.end(); ++it)
            if (it.key().size() > 80 || it.key() == "__proto__" || it.key() == "constructor" ||
                it.key() == "prototype" || !ValidStateValue(it.value(), depth + 1)) return false;
    }
    return true;
}
inline bool ValidCardState(const QJsonObject& state) {
    return QJsonDocument(state).toJson(QJsonDocument::Compact).size() <= 16384 && ValidStateValue(state);
}
inline bool ValidCardContent(const Card& card) {
    const bool image = !card.imagePath.isEmpty();
    return !card.name.trimmed().isEmpty() && card.name.size() <= 80 &&
        !card.name.contains('\n') && !card.name.contains('\r') && !card.name.contains('\t') &&
        (image || !card.html.isEmpty()) && card.html.toUtf8().size() <= 131072 &&
        (!image || card.html.isEmpty() || card.html.contains("callayers-image:local")) &&
        std::isfinite(card.contentW) && std::isfinite(card.contentH) &&
        card.contentW >= (image ? 1 : 180) && card.contentW <= 1280 &&
        card.contentH >= (image ? 1 : 100) && card.contentH <= 720 && ValidCardState(card.state);
}
inline void FitCardContent(Card& card) {
    double scale = std::min({card.w / card.contentW, (1280 - card.x) / card.contentW,
                             (720 - card.y) / card.contentH});
    card.w = card.contentW * scale;
    card.h = card.contentH * scale;
}
