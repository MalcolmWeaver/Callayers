#include "store.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
QString DefaultData() {
    auto env = qEnvironmentVariable("XDG_DATA_HOME");
    return (env.isEmpty() ? QDir::homePath() + "/.local/share" : env) + "/Callayers";
}
QString DefaultConfig() {
    auto env = qEnvironmentVariable("XDG_CONFIG_HOME");
    return (env.isEmpty() ? QDir::homePath() + "/.config" : env) + "/Callayers";
}
QJsonObject CardJson(const Card& card) {
    QJsonObject object;
    object["id"] = card.id;
    object["name"] = card.name;
    object["html"] = card.html;
    object["imagePath"] = card.imagePath;
    object["x"] = card.x;
    object["y"] = card.y;
    object["w"] = card.w;
    object["h"] = card.h;
    object["contentW"] = card.contentW;
    object["contentH"] = card.contentH;
    object["visible"] = card.visible;
    object["layer"] = card.layer;
    object["edited"] = double(card.edited);
    if (card.hasPrevious) {
        QJsonObject previous;
        previous["name"] = card.previousName;
        previous["html"] = card.previousHtml;
        previous["w"] = card.previousW;
        previous["h"] = card.previousH;
        object["previous"] = previous;
    }
    return object;
}
Card CardFrom(const QJsonObject& object) {
    Card card;
    card.id = object["id"].toString();
    card.name = object["name"].toString();
    card.html = object["html"].toString();
    card.imagePath = object["imagePath"].toString();
    card.x = object["x"].toDouble(48);
    card.y = object["y"].toDouble(48);
    card.w = object["w"].toDouble(480);
    card.h = object["h"].toDouble(270);
    card.contentW = object["contentW"].toDouble(card.w);
    card.contentH = object["contentH"].toDouble(card.h);
    card.visible = object["visible"].toBool(true);
    card.layer = object["layer"].toInt();
    card.edited = qint64(object["edited"].toDouble());
    if (object.contains("previous")) {
        auto previous = object["previous"].toObject();
        card.hasPrevious = true;
        card.previousName = previous["name"].toString();
        card.previousHtml = previous["html"].toString();
        card.previousW = previous["w"].toDouble();
        card.previousH = previous["h"].toDouble();
    }
    return card;
}
}

Store::Store(QString dataRoot, QString configRoot) : dataRoot_(dataRoot.isEmpty() ? DefaultData() : dataRoot), configRoot_(configRoot.isEmpty() ? DefaultConfig() : configRoot) {
    QDir().mkpath(dataRoot_);
    QDir().mkpath(configRoot_);
    QDir().mkpath(imageDirectory());
    load();
}

QString Store::imageDirectory() const { return dataRoot_ + "/images"; }

void Store::load() {
    QFile workspace(dataRoot_ + "/workspace.json");
    if (workspace.open(QIODevice::ReadOnly)) {
        auto root = QJsonDocument::fromJson(workspace.readAll()).object();
        for (const auto& value : root["cards"].toArray()) cards_.push_back(CardFrom(value.toObject()));
        for (const auto& value : root["annotations"].toArray()) {
            auto object = value.toObject();
            Annotation mark;
            mark.id = quint64(object["id"].toDouble());
            mark.kind = Tool(object["kind"].toInt());
            mark.color = object["color"].toString("#ffd166");
            mark.size = object["size"].toInt(1);
            mark.font = object["font"].toDouble(36);
            mark.box = QRectF(object["x"].toDouble(), object["y"].toDouble(), object["w"].toDouble(), object["h"].toDouble());
            mark.text = object["text"].toString();
            for (const auto& point : object["points"].toArray()) {
                auto pair = point.toArray();
                mark.points.push_back({pair.at(0).toDouble(), pair.at(1).toDouble()});
            }
            annotations_.push_back(mark);
        }
    }
    QFile preferences(configRoot_ + "/preferences.json");
    if (preferences.open(QIODevice::ReadOnly)) {
        auto root = QJsonDocument::fromJson(preferences.readAll()).object();
        preferences_.cameraIdentity = root["cameraIdentity"].toString();
        preferences_.cameraName = root["cameraName"].toString();
        preferences_.annotateOpen = root["annotateOpen"].toBool();
    }
}

void Store::setCards(QVector<Card> cards) { cards_ = std::move(cards); saveWorkspace(); }
void Store::setAnnotations(QVector<Annotation> annotations) { annotations_ = std::move(annotations); saveWorkspace(); }
void Store::setPreferences(Preferences preferences) { preferences_ = std::move(preferences); savePreferences(); }

void Store::saveWorkspace() {
    QJsonArray cards, annotations;
    for (const auto& card : cards_) cards.append(CardJson(card));
    for (const auto& mark : annotations_) {
        QJsonObject object;
        object["id"] = double(mark.id);
        object["kind"] = int(mark.kind);
        object["color"] = mark.color;
        object["size"] = mark.size;
        object["font"] = mark.font;
        object["x"] = mark.box.x();
        object["y"] = mark.box.y();
        object["w"] = mark.box.width();
        object["h"] = mark.box.height();
        object["text"] = mark.text;
        QJsonArray points;
        for (auto point : mark.points) points.append(QJsonArray{point.x(), point.y()});
        object["points"] = points;
        annotations.append(object);
    }
    QJsonObject root;
    root["cards"] = cards;
    root["annotations"] = annotations;
    QFile file(dataRoot_ + "/workspace.json");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void Store::savePreferences() {
    QJsonObject root;
    root["cameraIdentity"] = preferences_.cameraIdentity;
    root["cameraName"] = preferences_.cameraName;
    root["annotateOpen"] = preferences_.annotateOpen;
    QFile file(configRoot_ + "/preferences.json");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
