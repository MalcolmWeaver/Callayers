#pragma once
#include "auth.h"
#include "capture.h"
#include "model.h"
#include "output.h"
#include "preview.h"
#include "store.h"
#include <QMainWindow>

class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow();
private:
    bool workspaceAllowed() const;
    void buildSignIn();
    void buildWorkspace();
    void refresh();
    void refreshCards();
    void tick();
    void chooseCamera(int index);
    void save();
    void generate();
    void importImage();
    void signOut();
    void closeEvent(QCloseEvent* event) override;
    void pointerDown(QPointF logical);
    void pointerMove(QPointF logical);
    void pointerUp();
    int cardAt(QPointF logical) const;
    int annotationAt(QPointF logical) const;
    void beginText(QPointF logical, int existing);
    void finishText(bool commit);
    QString cameraMessage(CaptureError error) const;
    Store store_;
    AuthController auth_;
    Capture capture_;
    Output output_;
    QVector<Card> cards_;
    QVector<Annotation> annotations_;
    QVector<CameraChoice> cameras_;
    QStackedWidget* stack_ = nullptr;
    Preview* preview_ = nullptr;
    QListWidget* library_ = nullptr;
    QComboBox* camerasBox_ = nullptr;
    QPushButton* retryCamera_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* authMessage_ = nullptr;
    QLabel* chatHeading_ = nullptr;
    QLineEdit* search_ = nullptr;
    QPlainTextEdit* prompt_ = nullptr;
    QPlainTextEdit* textEdit_ = nullptr;
    QWidget* annotateBar_ = nullptr;
    QTimer* timer_ = nullptr;
    Tool tool_ = Tool::Select;
    QString color_ = "#ffd166";
    int size_ = 1;
    int selected_ = -1;
    int selectedAnnotation_ = -1;
    bool dragging_ = false;
    bool resizing_ = false;
    bool drawing_ = false;
    QPointF dragOrigin_, cardOrigin_;
    double originW_ = 0, originH_ = 0;
    quint64 editingText_ = 0;
    QString editingOriginal_;
    bool generating_ = false;
    bool outputWanted_ = false;
    bool capturePaused_ = false;
};
