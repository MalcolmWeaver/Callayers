#pragma once
#include <QDateTime>
#include <QObject>
#include <QString>

class AuthController : public QObject {
    Q_OBJECT
public:
    explicit AuthController(QObject* parent = nullptr);
    void restore();
    void signIn();
    void cancel();
    void signOut();
    bool allowed() const { return allowed_; }
    QString token() const;
    QString email() const { return email_; }
    QString message() const { return message_; }
    QString accountId() const { return uid_; }
    QString serviceUrl() const { return serviceUrl_; }
signals:
    void changed();
    void credentialsChanged();
private:
    struct Config { QString url, project, client, secret, apiKey; };
    struct Grant { QString serviceUrl, projectId, uid, refresh, token, email; QDateTime expires; };
    QString serviceRoot() const;
    Config loadConfig();
    Grant refresh(const Config& config, const Grant& current);
    void interactive(const Config& config);
    void accept(Grant grant);
    void fail(const QString& message, bool clearSaved);
    QByteArray http(const QString& url, const QByteArray& method, const QByteArray& body, const QString& type, const QString& token, int* status = nullptr);
    bool cancelled() const { return generation_ != activeGeneration_; }
    Config config_;
    QString serviceUrl_, email_, message_, token_, refresh_, uid_, projectId_;
    QDateTime expires_;
    bool allowed_ = false;
    bool renewing_ = false;
    unsigned generation_ = 0, activeGeneration_ = 0;
};
