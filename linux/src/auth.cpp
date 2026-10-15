#include "auth.h"
#include "auth_callback.h"
#include "secret.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QDesktopServices>
#include <QUrl>

namespace {
QByteArray RandomToken(int bytes) {
    QByteArray buffer(bytes, 0);
    QFile random("/dev/urandom");
    if (!random.open(QIODevice::ReadOnly) || random.read(buffer.data(), bytes) != bytes) throw QString("Couldn't prepare Google sign-in. Try again.");
    return buffer.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QString Required(const QJsonObject& object, const char* key, int maximum = 16384) {
    auto value = object.value(key).toString();
    if (value.isEmpty() || value.size() > maximum) throw QString("The sign-in service returned an unexpected response. Try again.");
    return value;
}
}

AuthController::AuthController(QObject* parent) : QObject(parent) {}

QString AuthController::token() const { return allowed_ && QDateTime::currentDateTimeUtc() < expires_ ? token_ : QString(); }

QString AuthController::serviceRoot() const {
    QString url = qEnvironmentVariable("CALLAYERS_SERVICE_URL");
    if (url.isEmpty()) url = qEnvironmentVariable("SERVICE_URL");
    if (url.isEmpty()) {
        QFile file(QCoreApplication::applicationDirPath() + "/service-url.txt");
        if (file.open(QIODevice::ReadOnly)) url = QString::fromUtf8(file.readAll());
    }
    if (url.isEmpty()) url = "https://callayers-v2-api-3fj7uh2a7a-uc.a.run.app";
    while (url.endsWith('/') || url.endsWith(' ') || url.endsWith('\n')) url.chop(1);
    return url;
}

QByteArray AuthController::http(const QString& url, const QByteArray& method, const QByteArray& body, const QString& type, const QString& bearer, int* status) {
    if (cancelled()) throw QString();
    QUrl parsed(url);
    if (!parsed.isValid() || parsed.hasFragment() || !parsed.userInfo().isEmpty()) throw QString("Callayers sign-in isn't configured correctly.");
    bool local = parsed.scheme() == "http" && parsed.host() == "127.0.0.1";
    if (parsed.scheme() != "https" && !local) throw QString("Callayers requires a secure sign-in service.");
    QNetworkRequest request(parsed);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("Accept", "application/json");
    if (!type.isEmpty()) request.setHeader(QNetworkRequest::ContentTypeHeader, type);
    if (!bearer.isEmpty()) request.setRawHeader("Authorization", "Bearer " + bearer.toUtf8());
    QNetworkAccessManager network;
    QNetworkReply* reply = network.sendCustomRequest(request, method, body);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(8000);
    loop.exec();
    if (!reply->isFinished()) { reply->abort(); throw QString("Couldn't connect to sign-in. Check your internet connection and Retry."); }
    int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto response = reply->readAll();
    reply->deleteLater();
    if (status) *status = code;
    if (code < 200 || code >= 300) {
        auto error = QJsonDocument::fromJson(response).object().value("error").toString();
        if (code == 401 || error.contains("TOKEN_EXPIRED") || error.contains("INVALID_REFRESH_TOKEN") || error.contains("USER_NOT_FOUND") || error.contains("USER_DISABLED") || error.contains("INVALID_ID_TOKEN"))
            throw QString("invalid-session");
        if (code == 403) throw QString("denied");
        throw QString("Couldn't finish sign-in. Try again.");
    }
    return response;
}

AuthController::Config AuthController::loadConfig() {
    Config config;
    config.url = serviceRoot();
    auto object = QJsonDocument::fromJson(http(config.url + "/client-config", "GET", {}, {}, {})).object();
    if (object.value("version").toInt() != 2) throw QString("Callayers sign-in isn't configured correctly.");
    config.project = Required(object, "firebaseProjectId", 128);
    config.client = Required(object, "googleDesktopClientId", 256);
    config.secret = Required(object, "googleDesktopClientSecret", 256);
    config.apiKey = Required(object, "firebaseApiKey", 256);
    return config;
}

AuthController::Grant AuthController::refresh(const Config& config, const Grant& current) {
    if (current.serviceUrl != config.url || current.projectId != config.project) throw QString("invalid-session");
    QUrlQuery form;
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("refresh_token", current.refresh);
    auto result = QJsonDocument::fromJson(http(QString("https://securetoken.googleapis.com/v1/token?key=") + QString::fromStdString(AuthUrlEncode(config.apiKey.toStdString())), "POST", form.query(QUrl::FullyEncoded).toUtf8(), "application/x-www-form-urlencoded", {})).object();
    Grant grant = current;
    grant.token = Required(result, "id_token");
    grant.refresh = Required(result, "refresh_token");
    if (Required(result, "user_id", 128) != current.uid) throw QString("invalid-session");
    auto seconds = Required(result, "expires_in", 10).toLongLong();
    if (seconds < 60 || seconds > 86400) throw QString("Couldn't finish sign-in. Try again.");
    grant.expires = QDateTime::currentDateTimeUtc().addSecs(seconds);
    auto account = QJsonDocument::fromJson(http(config.url + "/account", "GET", {}, {}, grant.token)).object();
    if (Required(account, "uid", 128) != current.uid || !account.value("accessAllowed").toBool()) throw QString("denied");
    grant.email = account.value("email").toString();
    return grant;
}

void AuthController::accept(Grant grant) {
    if (cancelled()) return;
    SavedSession saved{grant.serviceUrl, grant.projectId, grant.uid, grant.refresh};
    QString error;
    bool remembered = SaveSession(saved, error);
    allowed_ = true;
    token_ = grant.token;
    refresh_ = grant.refresh;
    uid_ = grant.uid;
    projectId_ = grant.projectId;
    serviceUrl_ = grant.serviceUrl;
    email_ = grant.email;
    expires_ = grant.expires;
    message_ = remembered ? QString() : error;
    emit changed();
}

void AuthController::fail(const QString& message, bool clearSaved) {
    if (cancelled()) return;
    allowed_ = false;
    token_.clear();
    if (clearSaved) { QString error; ClearSession(error); }
    message_ = message == "invalid-session" ? "Your sign-in expired. Sign in with Google again." : message == "denied" ? "This account can't use Callayers. Try another Google account." : message;
    emit changed();
}

void AuthController::interactive(const Config& config) {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) throw QString("Couldn't prepare Google sign-in. Try again.");
    QString redirect = "http://127.0.0.1:" + QString::number(server.serverPort()) + "/callback";
    QByteArray state = RandomToken(32), verifier = RandomToken(48);
    QByteArray challenge = QCryptographicHash::hash(verifier, QCryptographicHash::Sha256).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrlQuery query;
    query.addQueryItem("client_id", config.client);
    query.addQueryItem("redirect_uri", redirect);
    query.addQueryItem("response_type", "code");
    query.addQueryItem("scope", "openid email profile");
    query.addQueryItem("state", QString::fromLatin1(state));
    query.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    query.addQueryItem("code_challenge_method", "S256");
    query.addQueryItem("prompt", "select_account");
    QUrl authUrl("https://accounts.google.com/o/oauth2/v2/auth");
    authUrl.setQuery(query);
    message_ = "Finish signing in with Google in your browser.";
    emit changed();
    QDesktopServices::openUrl(authUrl);
    QString code;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 300000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (cancelled()) return;
        if (!server.waitForNewConnection(50)) continue;
        QTcpSocket* socket = server.nextPendingConnection();
        if (!socket->waitForReadyRead(2000)) { socket->deleteLater(); continue; }
        QByteArray request = socket->readAll();
        auto line = request.indexOf("\r\n");
        QString target;
        if (request.startsWith("GET ") && line > 4) target = QString::fromLatin1(request.mid(4, line - 4));
        auto path = target.section(' ', 0, 0).toStdString();
        auto callback = ParseAuthCallback(path, state.toStdString());
        auto reply = [&](int httpStatus, const QString& html) {
            QByteArray body = html.toUtf8();
            QByteArray response = "HTTP/1.1 " + QByteArray::number(httpStatus) + "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
            socket->write(response);
            socket->waitForBytesWritten(2000);
            socket->deleteLater();
        };
        if (callback.kind == CallbackKind::Ignore) { reply(400, "<!doctype html><title>Callayers</title><h1>Callayers</h1><p>This is not the expected sign-in callback. Return to Google to finish signing in.</p>"); continue; }
        server.close();
        if (callback.kind == CallbackKind::Cancelled) {
            reply(400, "<!doctype html><title>Callayers</title><h1>Callayers</h1><p>Sign-in was cancelled. Return to Callayers to try again.</p>");
            throw QString("Google sign-in was cancelled. Try again.");
        }
        code = QString::fromStdString(callback.code);
        QUrlQuery exchange;
        exchange.addQueryItem("client_id", config.client);
        exchange.addQueryItem("client_secret", config.secret);
        exchange.addQueryItem("code", code);
        exchange.addQueryItem("redirect_uri", redirect);
        exchange.addQueryItem("grant_type", "authorization_code");
        exchange.addQueryItem("code_verifier", QString::fromLatin1(verifier));
        try {
            auto google = QJsonDocument::fromJson(http("https://oauth2.googleapis.com/token", "POST", exchange.query(QUrl::FullyEncoded).toUtf8(), "application/x-www-form-urlencoded", {})).object();
            QJsonObject payload;
            payload["requestUri"] = redirect;
            payload["returnSecureToken"] = true;
            payload["postBody"] = QString("id_token=") + QString::fromUtf8(QUrl::toPercentEncoding(Required(google, "id_token"))) + "&providerId=google.com";
            auto firebase = QJsonDocument::fromJson(http(QString("https://identitytoolkit.googleapis.com/v1/accounts:signInWithIdp?key=") + QString::fromStdString(AuthUrlEncode(config.apiKey.toStdString())), "POST", QJsonDocument(payload).toJson(QJsonDocument::Compact), "application/json", {})).object();
            Grant grant;
            grant.serviceUrl = config.url;
            grant.projectId = config.project;
            grant.uid = Required(firebase, "localId", 128);
            grant.refresh = Required(firebase, "refreshToken");
            grant = refresh(config, grant);
            QString ticket;
            try {
                auto attribution = QJsonDocument::fromJson(http(config.url + "/attribution/ticket", "POST", {}, {}, grant.token)).object();
                auto value = attribution.value("ticket").toString();
                if (value.size() == 43) ticket = value;
            } catch (...) {}
            accept(grant);
            QByteArray location = "https://callayers.com/signed-in";
            if (!ticket.isEmpty()) location += "#ticket=" + ticket.toUtf8();
            QByteArray response = "HTTP/1.1 303 See Other\r\nLocation: " + location + "\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            socket->write(response);
            socket->waitForBytesWritten(2000);
            socket->deleteLater();
            return;
        } catch (...) {
            reply(400, "<!doctype html><title>Callayers</title><h1>Callayers</h1><p>Couldn't finish signing in. Return to Callayers and Retry.</p>");
            throw;
        }
    }
    throw QString("Google sign-in timed out. Try again.");
}

void AuthController::restore() {
    unsigned generation = ++generation_;
    activeGeneration_ = generation;
    message_ = "Checking your sign-in…";
    emit changed();
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != generation_) return;
        try {
            QString error;
            auto saved = LoadSession(error);
            if (!saved) {
                if (!error.isEmpty()) fail(error, false);
                else { message_ = "Sign in to use Callayers."; emit changed(); }
                return;
            }
            config_ = loadConfig();
            Grant current;
            current.serviceUrl = saved->serviceUrl;
            current.projectId = saved->projectId;
            current.uid = saved->uid;
            current.refresh = saved->refreshToken;
            accept(refresh(config_, current));
        } catch (const QString& message) { fail(message, message == "invalid-session" || message == "denied"); }
        catch (...) { fail("Couldn't confirm your sign-in. Sign in with Google to try again.", false); }
    });
}

void AuthController::signIn() {
    if (allowed_) return;
    unsigned generation = ++generation_;
    activeGeneration_ = generation;
    message_ = "Opening Google sign-in…";
    emit changed();
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != generation_) return;
        try {
            config_ = loadConfig();
            interactive(config_);
        } catch (const QString& message) { if (!message.isEmpty()) fail(message, false); }
        catch (...) { fail("Couldn't finish sign-in. Try again.", false); }
    });
}

void AuthController::cancel() {
    if (allowed_) return;
    ++generation_;
    message_ = "Sign-in cancelled. Try again.";
    emit changed();
}

void AuthController::signOut() {
    ++generation_;
    allowed_ = false;
    token_.clear();
    refresh_.clear();
    QString error;
    message_ = ClearSession(error) ? "Sign in to use Callayers." : error;
    emit changed();
}
