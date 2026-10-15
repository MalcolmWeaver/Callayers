#include "secret.h"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QJsonDocument>
#include <QJsonObject>

struct SecretValue { QDBusObjectPath session; QByteArray parameters, value; QString contentType; };
Q_DECLARE_METATYPE(SecretValue)
QDBusArgument& operator<<(QDBusArgument& argument, const SecretValue& secret) {
    argument.beginStructure();
    argument << secret.session << secret.parameters << secret.value << secret.contentType;
    argument.endStructure();
    return argument;
}
const QDBusArgument& operator>>(const QDBusArgument& argument, SecretValue& secret) {
    argument.beginStructure();
    argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    argument.endStructure();
    return argument;
}
namespace {
const QString Service = "org.freedesktop.secrets";
const QString ServicePath = "/org/freedesktop/secrets";
const QString Collection = "/org/freedesktop/secrets/aliases/default";

struct Session {
    QDBusObjectPath path;
    ~Session() {
        if (path.path().isEmpty()) return;
        QDBusMessage close = QDBusMessage::createMethodCall(Service, path.path(), "org.freedesktop.Secret.Session", "Close");
        QDBusConnection::sessionBus().call(close);
    }
};

bool Open(Session& session, QString& error) {
    qDBusRegisterMetaType<SecretValue>();
    auto reply = QDBusConnection::sessionBus().call(QDBusMessage::createMethodCall(Service, ServicePath, "org.freedesktop.Secret.Service", "OpenSession")
        << "plain" << QVariant::fromValue(QDBusVariant(QString())));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        error = "The session could not be saved. A desktop keyring is not available.";
        return false;
    }
    session.path = reply.arguments().at(1).value<QDBusObjectPath>();
    return !session.path.path().isEmpty();
}

QList<QDBusObjectPath> Matching(QString& error) {
    qDBusRegisterMetaType<QMap<QString, QString>>();
    QDBusInterface collection(Service, Collection, "org.freedesktop.Secret.Collection", QDBusConnection::sessionBus());
    auto reply = collection.call("SearchItems", QVariant::fromValue(QMap<QString, QString>{{"application", "callayers"}}));
    if (reply.type() != QDBusMessage::ReplyMessage) { error = "The session could not be saved. A desktop keyring is not available."; return {}; }
    return reply.arguments().value(0).value<QList<QDBusObjectPath>>();
}
}

std::optional<SavedSession> LoadSession(QString& error) {
    if (!QDBusConnection::sessionBus().isConnected()) { error = "The session could not be saved. A desktop keyring is not available."; return {}; }
    Session session;
    if (!Open(session, error)) return {};
    auto items = Matching(error);
    if (!error.isEmpty() || items.isEmpty()) return {};
    QDBusMessage message = QDBusMessage::createMethodCall(Service, items.first().path(), "org.freedesktop.Secret.Item", "GetSecret");
    message << QVariant::fromValue(session.path);
    auto reply = QDBusConnection::sessionBus().call(message);
    if (reply.type() != QDBusMessage::ReplyMessage) { error = "The session could not be saved. A desktop keyring is not available."; return {}; }
    auto secret = qdbus_cast<SecretValue>(reply.arguments().value(0));
    auto object = QJsonDocument::fromJson(secret.value).object();
    SavedSession saved{object["serviceUrl"].toString(), object["projectId"].toString(), object["uid"].toString(), object["refreshToken"].toString()};
    if (saved.refreshToken.isEmpty()) return {};
    return saved;
}

bool SaveSession(const SavedSession& session, QString& error) {
    if (!QDBusConnection::sessionBus().isConnected()) { error = "The session could not be saved. A desktop keyring is not available."; return false; }
    Session bus;
    if (!Open(bus, error)) return false;
    QJsonObject object;
    object["serviceUrl"] = session.serviceUrl;
    object["projectId"] = session.projectId;
    object["uid"] = session.uid;
    object["refreshToken"] = session.refreshToken;
    SecretValue secret{bus.path, {}, QJsonDocument(object).toJson(QJsonDocument::Compact), "application/json"};
    QVariantMap properties;
    properties["org.freedesktop.Secret.Item.Label"] = "Callayers session";
    properties["org.freedesktop.Secret.Item.Attributes"] = QVariant::fromValue(QMap<QString, QString>{{"application", "callayers"}});
    QDBusMessage message = QDBusMessage::createMethodCall(Service, Collection, "org.freedesktop.Secret.Collection", "CreateItem");
    message << properties << QVariant::fromValue(secret) << true;
    auto reply = QDBusConnection::sessionBus().call(message);
    if (reply.type() != QDBusMessage::ReplyMessage) { error = "The session could not be saved. A desktop keyring is not available."; return false; }
    error.clear();
    return true;
}

bool ClearSession(QString& error) {
    if (!QDBusConnection::sessionBus().isConnected()) return true;
    Session session;
    if (!Open(session, error)) return false;
    for (const auto& item : Matching(error)) {
        auto reply = QDBusConnection::sessionBus().call(QDBusMessage::createMethodCall(Service, item.path(), "org.freedesktop.Secret.Item", "Delete"));
        if (reply.type() != QDBusMessage::ReplyMessage) { error = "Signed out. Couldn't remove your saved sign-in. Retry."; return false; }
    }
    error.clear();
    return true;
}
