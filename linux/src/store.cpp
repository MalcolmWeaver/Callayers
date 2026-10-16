#include "store.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include "card_validation.h"
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
    object["state"] = card.state;
    object["contentVersion"] = double(card.contentVersion);
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
    card.state = object["state"].toObject();
    card.contentVersion = quint64(object["contentVersion"].toDouble());
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
    if (workspace.exists() && !workspace.open(QIODevice::ReadOnly)) { loadFailed_=true; problem_="Couldn't read your saved workspace. The original file has been kept."; return; }
    if (workspace.isOpen()) {
        QJsonParseError parse;
        auto document = QJsonDocument::fromJson(workspace.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject() || !document.object()["cards"].isArray() || !document.object()["annotations"].isArray()) {
            problem_="Couldn't read your saved workspace. The original file has been kept."; loadFailed_=true; return;
        }
        auto root = document.object();
        for (const auto& value : root["cards"].toArray()) {
            auto card=CardFrom(value.toObject());
            if (!ValidCardContent(card) || card.id.isEmpty() || card.x<0 || card.y<0 || card.w<=0 || card.h<=0 || card.x+card.w>1280.1 || card.y+card.h>720.1) {
                problem_="Couldn't read your saved cards. The original file has been kept."; loadFailed_=true; return;
            }
            for(const auto& existing:cards_)if(existing.id==card.id) { loadFailed_=true;problem_="Saved card identities conflict. The original file has been kept.";return; }
            if(card.hasPrevious) {
                auto backup=card;backup.name=card.previousName;backup.html=card.previousHtml;backup.contentW=card.previousW;backup.contentH=card.previousH;
                if(!ValidCardContent(backup)) {loadFailed_=true;problem_="Couldn't read the saved previous version. The original file has been kept.";return;}
            }
            cards_.push_back(card);
        }
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
                if (pair.size()!=2 || !pair[0].isDouble() || !pair[1].isDouble()) { loadFailed_=true; problem_="Couldn't read saved annotations. The original file has been kept."; return; }
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

bool Store::setWorkspace(const QVector<Card>& cards, const QVector<Annotation>& annotations) {
    cards_=cards; annotations_=annotations; saveWorkspace(); return problem_.isEmpty();
}

void Store::setCards(QVector<Card> cards) { cards_ = std::move(cards); saveWorkspace(); }
void Store::setAnnotations(QVector<Annotation> annotations) { annotations_ = std::move(annotations); saveWorkspace(); }
void Store::setPreferences(Preferences preferences) { preferences_ = std::move(preferences); savePreferences(); }

void Store::saveWorkspace() {
    if (loadFailed_) return;
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
    QSaveFile file(dataRoot_ + "/workspace.json");
    const auto bytes=QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) problem_="Couldn't save your workspace. Retry. Quitting may lose unsaved changes.";
    else problem_.clear();
}

void Store::savePreferences() {
    QJsonObject root;
    root["cameraIdentity"] = preferences_.cameraIdentity;
    root["cameraName"] = preferences_.cameraName;
    root["annotateOpen"] = preferences_.annotateOpen;
    QSaveFile file(configRoot_ + "/preferences.json");
    const auto bytes=QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) problem_="Couldn't save preferences. Retry.";
}
