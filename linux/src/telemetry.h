#pragma once
#include "auth.h"
#include "model.h"
#include <QHash>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
class QNetworkReply;
// Bounded, best-effort events. Never captures prompts, text, image paths or pixels.
class Telemetry : public QObject {
public:
    explicit Telemetry(AuthController& auth, QObject* parent=nullptr);
    void content(const QString& kind, const QString& cardId={});
    void preview(const QVector<Card>& cards, bool visible);
    void flush();
private:
    void account();
    void record(const QString& event, QJsonObject properties);
    AuthController& auth_;
    QNetworkAccessManager network_;
    QTimer timer_;
    QPointer<QNetworkReply> active_;
    QString owner_, url_, session_;
    QJsonArray pending_;
    QHash<QString,QString> previewCards_;
    quint64 epoch_=0;
    int attempts_=0;
};
