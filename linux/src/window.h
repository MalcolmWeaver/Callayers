#pragma once
#include "auth.h"
#include "capture.h"
#include "model.h"
#include "output.h"
#include "preview.h"
#include "store.h"
#include "scene_renderer.h"
#include <QPointer>
#include <QElapsedTimer>
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
class TextEditorWidget;
class Telemetry;
class PromptEditor;
class QNetworkReply;
class QNetworkAccessManager;

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
    void positionTextEditor();
    void selectTool(Tool tool);
    void cancelGeneration();
    void refreshAttachments();
    bool eventFilter(QObject* watched, QEvent* event) override;
    QString cameraMessage(CaptureError error) const;
    Store store_;
    AuthController auth_;
    Capture capture_;
    Output output_;
    SceneRenderer renderer_;
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
    PromptEditor* prompt_ = nullptr;
    QWidget* attachmentsPanel_ = nullptr;
    TextEditorWidget* textEdit_ = nullptr;
    QWidget* annotateBar_ = nullptr;
    QTimer* timer_ = nullptr;
    Telemetry* telemetry_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
    QPointer<QNetworkReply> generationReply_;
    QPushButton* retrySave_ = nullptr;
    QPushButton* cancelGeneration_ = nullptr;
    QLabel* operationStatus_ = nullptr;
    QElapsedTimer checkpointTimer_;
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
    Annotation textDraft_;
    Annotation annotationOrigin_;
    bool editingNew_ = false;
    bool cardInput_ = false;
    bool rendererPaused_ = false;
    bool offlinePreview_ = false;
    bool skipSignIn_ = false;
    QPointF lastPointer_;
    int resizeCorner_ = -1;
    quint64 generationEpoch_ = 0;
    bool generating_ = false;
    bool outputWanted_ = false;
    bool capturePaused_ = false;
};
