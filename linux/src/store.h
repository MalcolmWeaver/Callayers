#pragma once
#include "model.h"

struct Preferences { QString cameraIdentity, cameraName; bool annotateOpen = false; };

class Store {
public:
    explicit Store(QString dataRoot = {}, QString configRoot = {});
    QVector<Card> cards() const { return cards_; }
    QVector<Annotation> annotations() const { return annotations_; }
    Preferences preferences() const { return preferences_; }
    bool setWorkspace(const QVector<Card>& cards, const QVector<Annotation>& annotations);
    QString problem() const { return problem_; }
    void setCards(QVector<Card> cards);
    void setAnnotations(QVector<Annotation> annotations);
    void setPreferences(Preferences preferences);
    QString imageDirectory() const;
private:
    void load();
    void saveWorkspace();
    void savePreferences();
    QString dataRoot_, configRoot_;
    QVector<Card> cards_;
    QVector<Annotation> annotations_;
    Preferences preferences_;
    QString problem_;
    bool loadFailed_ = false;
};
