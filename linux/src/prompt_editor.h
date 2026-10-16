#pragma once
#include <QPlainTextEdit>
#include <QMimeData>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextCursor>
#include <functional>
class PromptEditor : public QPlainTextEdit {
public:
    QJsonArray attachments;
    std::function<void()> attachmentsChanged;
    std::function<void(const QString&)> message;
    bool busy=false;
    using QPlainTextEdit::QPlainTextEdit;
    qsizetype draftBytes() const {
        qsizetype size=toPlainText().toUtf8().size();
        for(const auto& a:attachments)size+=a.toObject()["text"].toString().toUtf8().size();
        return size;
    }
protected:
    void insertFromMimeData(const QMimeData* source) override {
        if(busy)return;
        if(!source->hasText()){if(message)message("Paste plain text here.");return;}
        const QString text=source->text();const bool attach=text.size()>=1000&&!text.trimmed().isEmpty();
        const auto selected=textCursor().selectedText().toUtf8().size();
        if(draftBytes()+text.toUtf8().size()-(attach?0:selected)>100000 || (attach&&attachments.size()>=8)) {
            if(message)message("This paste would exceed 100 KB of text or 8 attachments. Your draft was kept; the paste is still on your clipboard.");return;
        }
        if(attach) {
            auto title=text.trimmed().left(72);
            if(!title.isEmpty()&&title.back().isHighSurrogate())title.chop(1);
            for(auto& c:title)if(c.unicode()<32)c=' ';
            attachments.append(QJsonObject{{"name",title},{"text",text}});
            if(attachmentsChanged)attachmentsChanged();
        } else QPlainTextEdit::insertFromMimeData(source);
        if(message)message({});
    }
};
