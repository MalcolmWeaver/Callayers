#include "telemetry.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QDateTime>
#include <QCoreApplication>
#include <QUuid>
namespace { QString Id(){return QUuid::createUuid().toString(QUuid::WithoutBraces);} }
Telemetry::Telemetry(AuthController& auth,QObject* parent):QObject(parent),auth_(auth) {
    connect(&auth_,&AuthController::credentialsChanged,this,[this]{account();});
    connect(&auth_,&AuthController::changed,this,[this]{account();});
    connect(&timer_,&QTimer::timeout,this,[this]{flush();});timer_.start(5000);
}
void Telemetry::account() {
    QString owner=auth_.allowed()?auth_.accountId():QString();
    if(owner==owner_&&auth_.serviceUrl()==url_)return;
    ++epoch_;if(active_){active_->abort();active_=nullptr;}
    owner_=owner;url_=auth_.serviceUrl();pending_={};previewCards_.clear();attempts_=0;
    session_=owner.isEmpty()?QString():Id();
    if(!owner.isEmpty())record("session_started",{});
}
void Telemetry::record(const QString& event,QJsonObject properties) {
    if(session_.isEmpty()||pending_.size()>=128)return;
    pending_.append(QJsonObject{{"id",Id()},{"sessionId",session_},{"timestamp",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"appVersion",QCoreApplication::applicationVersion().isEmpty()?"0.3.0":QCoreApplication::applicationVersion()},{"event",event},{"properties",properties}});
}
void Telemetry::content(const QString& kind,const QString& id) {
    if(kind=="annotation")record("content_created",{{"kind",kind}});
    else if((kind=="ai_create"||kind=="ai_revision"||kind=="image_import")&&!id.isEmpty())record("content_created",{{"kind",kind},{"card_id",id}});
}
void Telemetry::preview(const QVector<Card>& cards,bool visible) {
    QHash<QString,QString> current;
    if(visible)for(const auto& card:cards)if(card.visible)current.insert(card.id,card.imagePath.isEmpty()?"ai":"image");
    for(auto i=previewCards_.begin();i!=previewCards_.end();++i)if(!current.contains(i.key()))record("card_usage_changed",{{"card_id",i.key()},{"card_type",i.value()},{"destination","preview"},{"applied",false}});
    for(auto i=current.begin();i!=current.end();++i)if(!previewCards_.contains(i.key()))record("card_usage_changed",{{"card_id",i.key()},{"card_type",i.value()},{"destination","preview"},{"applied",true}});
    previewCards_=current;
}
void Telemetry::flush() {
    if(active_||pending_.isEmpty()||auth_.token().isEmpty()||owner_!=auth_.accountId())return;
    QUrl url(url_+"/events");
    if(url.scheme()!="https"&&!(url.scheme()=="http"&&url.host()=="127.0.0.1"))return;
    if(!url.userInfo().isEmpty()||url.hasFragment())return;
    while(!pending_.isEmpty()&&QDateTime::fromString(pending_[0].toObject()["timestamp"].toString(),Qt::ISODateWithMs).secsTo(QDateTime::currentDateTimeUtc())>300)pending_.removeFirst();
    if(pending_.isEmpty())return;
    QJsonArray batch;for(int i=0;i<std::min(32,int(pending_.size()));++i)batch.append(pending_[i]);
    QNetworkRequest request(url);request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setRawHeader("Authorization","Bearer "+auth_.token().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setTransferTimeout(6000);
    auto reply=network_.post(request,QJsonDocument(QJsonObject{{"events",batch}}).toJson(QJsonDocument::Compact));active_=reply;
    auto deadline=new QTimer(reply);deadline->setSingleShot(true);connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort);deadline->start(8000);
    const auto epoch=epoch_;const int count=batch.size();
    connect(reply,&QNetworkReply::finished,this,[this,reply,epoch,count]{
        reply->deleteLater();if(epoch!=epoch_)return;active_=nullptr;
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if((status>=200&&status<300)||(status>=400&&status<500&&status!=401&&status!=408&&status!=429)||++attempts_>=3) {
            for(int i=0;i<count&&!pending_.isEmpty();++i)pending_.removeFirst();attempts_=0;
        }
    });
}
