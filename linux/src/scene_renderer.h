#pragma once
#include "model.h"
#include <QImage>
#include <memory>
class QKeyEvent;

// Owns the same isolated HTML documents and scene used by the Windows app.
// All methods run on the UI thread; no renderer object crosses thread boundaries.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();
    bool render(QVector<Card>& cards, const QVector<Annotation>& annotations, quint64 privateTextId = 0);
    bool checkpoint(QVector<Card>& cards);
    void suspend(QVector<Card>& cards);
    void mouse(int type, QPointF point);
    void scroll(QPointF point, int delta);
    void key(QKeyEvent* event);
    void blur();
    const QImage& image() const;
    const QImage& previewImage() const;
    QString problem() const;
    QString evaluate(const QString& script, const QString& frame = {});
private:
    bool renderScene(QVector<Card>& cards, const QVector<Annotation>& annotations);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
