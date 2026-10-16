#pragma once
#include "model.h"
#include <QWidget>
#include <memory>
class TextEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit TextEditorWidget(QWidget* parent = nullptr);
    ~TextEditorWidget();
    void begin(const Annotation& text, double scale);
    void setScale(double scale);
    QString toPlainText() const;
    double textHeight() const;
    void finishInput();
    bool ready() const;
signals:
    void textChanged();
    void finished(bool commit);
protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void inputMethodEvent(QInputMethodEvent*) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
private:
    void updateView();
    void readState();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
