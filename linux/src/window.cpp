#include "window.h"
#include "compose.h"
#include "camera_selector.h"
#include "card_validation.h"
#include "annotation_geometry.h"
#include "card_revision.h"
#include "text_editor_widget.h"
#include "telemetry.h"
#include "prompt_editor.h"
#include <QButtonGroup>
#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QTextDocument>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QScrollArea>
#include <QBuffer>
#include <QStyledItemDelegate>
#include <QCoreApplication>
#include <algorithm>
#include <chrono>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QJsonArray>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtMath>

namespace {
class CardRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        auto content=option;content.rect.adjust(0,0,-24,0);
        QStyledItemDelegate::paint(painter,content,index);
        if(index.data(Qt::UserRole).isValid()) { painter->save(); painter->setPen(QColor("#bbb8c9")); painter->drawText(option.rect.adjusted(option.rect.width()-26,0,0,0),Qt::AlignCenter,QString::fromUtf8("⋮"));painter->restore(); }
    }
};
QString NewId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

MainWindow::MainWindow() : store_(), auth_(this) {
#ifdef CALLAYERS_DEVELOPMENT
    setWindowTitle("Callayers — Development");
#else
    setWindowTitle("Callayers");
#endif
    resize(1280, 800);
    setMinimumSize(1000, 650);
    network_ = new QNetworkAccessManager(this);
    telemetry_ = new Telemetry(auth_, this);
    checkpointTimer_.start();
#ifdef CALLAYERS_DEVELOPMENT
    offlinePreview_ = QCoreApplication::arguments().contains("--offline-preview");
    skipSignIn_ = QCoreApplication::arguments().contains("--skip-sign-in");
#endif
    cards_ = store_.cards();
    annotations_ = store_.annotations();
    stack_ = new QStackedWidget;
    setCentralWidget(stack_);
    buildSignIn();
    buildWorkspace();
    buildCameraRecovery();
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MainWindow::tick);
    timer_->start(1000 / OutputFps);
    connect(&auth_, &AuthController::changed, this, &MainWindow::refresh);
    connect(&auth_, &AuthController::changed, this, [this] { if (auth_.allowed()) { showNormal(); raise(); activateWindow(); } });
    auto file = menuBar()->addMenu("File");
    if (!skipSignIn_) file->addAction("Sign out", this, &MainWindow::signOut);
    file->addAction("Quit", this, &QWidget::close);
    if (!skipSignIn_) auth_.restore();
    refresh();
    if (!store_.problem().isEmpty()) operationStatus_->setText(store_.problem());
}

bool MainWindow::sessionAllowed() const {
    return skipSignIn_ || auth_.allowed();
}

bool MainWindow::workspaceAllowed() const {
    return sessionAllowed() && (offlinePreview_ || (cameraReady_ && capture_.error() == CaptureError::None && !capture_.latest().isNull()));
}

void MainWindow::signOut() { finishText(true); cancelGeneration(); renderer_.suspend(cards_); save(); auth_.signOut(); output_.stop(); capture_.stop(); outputWanted_ = false; }

void MainWindow::buildSignIn() {
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->addStretch();
    auto title = new QLabel("Callayers");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size:32px; font-weight:650;");
    auto button = new QPushButton("Sign in with Google");
    button->setStyleSheet("background:#a39bfa; color:#201d33; font-weight:650; padding:12px 18px;");
    authMessage_ = new QLabel("Sign in to use Callayers.");
    authMessage_->setAlignment(Qt::AlignCenter);
    authMessage_->setWordWrap(true);
    layout->addWidget(title);
    layout->addWidget(button, 0, Qt::AlignCenter);
    layout->addWidget(authMessage_);
    layout->addStretch();
    connect(button, &QPushButton::clicked, &auth_, &AuthController::signIn);
    stack_->addWidget(page);
}

void MainWindow::buildWorkspace() {
    auto page = new QWidget;
    workspacePage_ = page;
    auto column = new QVBoxLayout(page);
    column->setContentsMargins(16, 12, 16, 16);
    column->setSpacing(14);
    auto header = new QHBoxLayout;
    auto logo = new QLabel;
    QString assets=QCoreApplication::applicationDirPath()+"/assets/callayers.png";
    if(!QFile::exists(assets))assets=QCoreApplication::applicationDirPath()+"/../share/callayers/assets/callayers.png";
    logo->setPixmap(QPixmap(assets).scaled(34,34,Qt::KeepAspectRatio,Qt::SmoothTransformation));
    header->addWidget(logo);
    auto mark = new QLabel("Callayers");
    mark->setStyleSheet("font-size:24px; font-weight:650; padding:8px 10px;");
    camerasBox_ = new QComboBox;
    camerasBox_->setAccessibleName("Webcam");
    camerasBox_->setObjectName("cameraSelector");
    retryCamera_ = new QPushButton("Refresh cameras");
    retryCamera_->setObjectName("retryCamera");
    status_ = new QLabel("In your calling app, choose Callayers");
    status_->setObjectName("cameraStatus");
    auto annotate = new QPushButton("Annotate");
    annotateButton_ = annotate;
    header->addWidget(mark);
    header->addStretch();
    header->addWidget(annotate);
    camerasBox_->setFixedWidth(240);
    header->addWidget(camerasBox_);
    header->addWidget(retryCamera_);
    status_->setWordWrap(true);
    status_->setStyleSheet("color:#aaa8b6; font-size:12px;");
    column->addLayout(header);
    auto body = new QHBoxLayout;
    auto sidebar = new QVBoxLayout;
    auto cardsHeading = new QLabel("Cards");
    cardsHeading->setObjectName("sectionHeading");
    sidebar->addWidget(cardsHeading);
    auto create = new QPushButton("+  Create with AI");
    create->setObjectName("primaryAction");
    auto import = new QPushButton("Import image");
    auto hide = new QPushButton("Hide all");
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search cards");
    library_ = new QListWidget;
    library_->setItemDelegate(new CardRowDelegate(library_));
    library_->viewport()->installEventFilter(this);
    chatHeading_ = new QLabel("New card");
    prompt_ = new PromptEditor;
    prompt_->setObjectName("cardPrompt");
    prompt_->attachmentsChanged=[this]{refreshAttachments();};
    prompt_->message=[this](const QString& message){operationStatus_->setText(message);};
    prompt_->setPlaceholderText("Describe the card");
    prompt_->setFixedHeight(90);
    auto send = new QPushButton("Send");
    send->setObjectName("primaryAction");
    cancelGeneration_ = new QPushButton("Cancel");
    cancelGeneration_->hide();
    connect(cancelGeneration_, &QPushButton::clicked, this, &MainWindow::cancelGeneration);
    operationStatus_ = new QLabel;
    operationStatus_->setObjectName("operationStatus");
    operationStatus_->setWordWrap(true);
    operationStatus_->setStyleSheet("color:#bbb8c9; font-size:12px;");
    sidebar->addWidget(create);
    auto libraryActions = new QHBoxLayout;
    libraryActions->addWidget(import); libraryActions->addWidget(hide);
    sidebar->addLayout(libraryActions);
    sidebar->addWidget(search_);
    sidebar->addWidget(library_, 1);
    sidebar->addWidget(chatHeading_);
    auto attachmentScroll=new QScrollArea;
    attachmentScroll->setObjectName("attachmentScroll");
    attachmentScroll->setWidgetResizable(true);attachmentScroll->setMaximumHeight(160);
    attachmentsPanel_=new QWidget;
    attachmentsPanel_->setLayout(new QVBoxLayout);
    attachmentScroll->setWidget(attachmentsPanel_);attachmentScroll->hide();
    sidebar->addWidget(attachmentScroll);
    sidebar->addWidget(prompt_);
    auto promptActions = new QHBoxLayout;
    promptActions->addWidget(send, 1); promptActions->addWidget(cancelGeneration_);
    sidebar->addLayout(promptActions);
    sidebar->addWidget(operationStatus_);
    retrySave_=new QPushButton("Retry saving");retrySave_->hide();
    connect(retrySave_,&QPushButton::clicked,this,&MainWindow::save);
    sidebar->addWidget(retrySave_);
    sidebar->addWidget(status_);
    auto side = new QWidget;
    libraryPanel_ = side;
    side->setLayout(sidebar);
    side->setFixedWidth(292);
    side->setObjectName("libraryPanel");
    sidebar->setContentsMargins(12, 12, 12, 12);
    sidebar->setSpacing(10);
    preview_ = new Preview;
    preview_->installEventFilter(this);
    textEdit_ = new TextEditorWidget(preview_);
    textEdit_->setObjectName("annotationEditor");
    textEdit_->installEventFilter(this); textEdit_->hide();
    connect(textEdit_, &TextEditorWidget::finished, this, &MainWindow::finishText);
    annotateBar_ = new QWidget;
    auto tools = new QHBoxLayout(annotateBar_);
    tools->setContentsMargins(6, 6, 6, 6);
    tools->setSpacing(5);
    auto toolGroup = new QButtonGroup(this);
    const QList<QPair<QString, Tool>> names{{"Select", Tool::Select}, {"Pen", Tool::Pen}, {"Rectangle", Tool::Rectangle}, {"Ellipse", Tool::Ellipse}, {"Line", Tool::Line}, {"Arrow", Tool::Arrow}, {"Text", Tool::Text}, {"Eraser", Tool::Eraser}};
    const QStringList icons{"↖", "✎", "□", "○", "╱", "↗", "T", "⌫"};
    for (int i = 0; i < names.size(); ++i) {
        const auto [label, value] = names[i];
        auto button = new QPushButton(icons[i]);
        button->setObjectName("tool" + label);
        button->setAccessibleName(label); button->setToolTip(label);
        button->setFixedSize(36, 34); button->setCheckable(true);
        toolGroup->addButton(button, int(value));
        button->setChecked(value == Tool::Select);
        connect(button, &QPushButton::clicked, this, [this, value] { selectTool(value); });
        tools->addWidget(button);
    }
    auto sizes = new QComboBox;
    sizes->setObjectName("annotationSize"); sizes->setAccessibleName("Annotation size");
    sizes->addItems({"Small", "Medium", "Large"}); sizes->setCurrentIndex(size_);
    connect(sizes, &QComboBox::activated, this, [this](int value) {
        if (!workspaceAllowed()) return;
        finishText(true); size_ = value;
        if (selectedAnnotation_ >= 0 && selectedAnnotation_ < annotations_.size()) {
            auto& mark = annotations_[selectedAnnotation_];
            mark.size = value;
            if (mark.kind == Tool::Text) { double scale = TextSizes[value] / mark.font; mark.font = TextSizes[value]; mark.box.setSize(mark.box.size() * scale); }
            save();
        }
    });
    tools->addWidget(sizes);
    auto colors = new QButtonGroup(this);
    for (auto swatch : {"#ffd166", "#f87171", "#67e8f9", "#86efac", "#ffffff", "#111111"}) {
        auto button = new QPushButton;
        button->setFixedSize(22, 22); button->setCheckable(true); colors->addButton(button);
        button->setAccessibleName(QString("Color %1").arg(swatch)); button->setToolTip(swatch);
        button->setChecked(color_ == swatch);
        button->setStyleSheet(QString("QPushButton {background:%1; border:2px solid #55535e; border-radius:11px; padding:0;} QPushButton:checked {border:3px solid #a39bfa;}").arg(swatch));
        connect(button, &QPushButton::clicked, this, [this, swatch] {
            if (!workspaceAllowed()) return;
            finishText(true); color_ = swatch;
            if (selectedAnnotation_ >= 0 && selectedAnnotation_ < annotations_.size()) { annotations_[selectedAnnotation_].color = color_; save(); }
        });
        tools->addWidget(button);
    }
    tools->addStretch();
    auto clear = new QPushButton("Clear"); clear->setToolTip("Clear annotations");
    tools->addWidget(clear);
    auto previewColumn = new QVBoxLayout;
    previewColumn->addWidget(annotateBar_);
    previewStack_ = new QStackedWidget;
    previewStack_->setObjectName("previewArea");
    previewStack_->addWidget(preview_);
    previewColumn->addWidget(previewStack_, 1);
    annotateBar_->setVisible(store_.preferences().annotateOpen);
    annotate->setCheckable(true);
    annotate->setChecked(store_.preferences().annotateOpen);
    body->addWidget(side);
    body->addLayout(previewColumn, 1);
    column->addLayout(body, 1);
    connect(annotate, &QPushButton::clicked, this, [this] {
        if (!workspaceAllowed()) return;
        finishText(true);
        annotateBar_->setVisible(!annotateBar_->isVisible());
        if (!annotateBar_->isVisible()) selectTool(Tool::Select);
        auto preferences = store_.preferences();
        preferences.annotateOpen = annotateBar_->isVisible();
        store_.setPreferences(preferences);
    });
    // Activation also permits retrying the currently selected camera.
    connect(camerasBox_, &QComboBox::activated, this, &MainWindow::chooseCamera);
    connect(retryCamera_, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(create, &QPushButton::clicked, this, [this] { if (!workspaceAllowed()) return; selected_ = -1; library_->clearSelection(); chatHeading_->setText("New card"); prompt_->setFocus(); });
    connect(import, &QPushButton::clicked, this, &MainWindow::importImage);
    connect(hide, &QPushButton::clicked, this, [this] { if (!workspaceAllowed()) return; for (auto& card : cards_) card.visible = false; save(); refreshCards(); });
    connect(send, &QPushButton::clicked, this, &MainWindow::generate);
    connect(search_, &QLineEdit::textChanged, this, &MainWindow::refreshCards);
    connect(library_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!workspaceAllowed()) return;
        int index = item->data(Qt::UserRole).toInt();
        if (index < 0 || index >= cards_.size()) return;
        cards_[index].visible = item->checkState() == Qt::Checked;
        auto name = item->text().trimmed();
        if (name.isEmpty() || name.size() > 80 || name.contains('\n')) { refreshCards(); return; }
        if (cards_[index].name != name) ++cards_[index].contentVersion;
        cards_[index].name = name;
        cards_[index].edited = QDateTime::currentMSecsSinceEpoch();
        save();
    });
    connect(library_, &QListWidget::currentRowChanged, this, [this](int row) {
        auto item = library_->item(row);
        selected_ = item && item->data(Qt::UserRole).isValid() ? item->data(Qt::UserRole).toInt() : -1;
        chatHeading_->setText(selected_ >= 0 ? "Edit “" + cards_.value(selected_).name + "”" : "New card");
    });
    library_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(library_, &QListWidget::customContextMenuRequested, this, [this](QPoint position) {
        if (!workspaceAllowed()) return;
        auto item = library_->itemAt(position);
        if (!item || !item->data(Qt::UserRole).isValid()) return;
        const QString id=cards_.value(item->data(Qt::UserRole).toInt()).id;
        auto resolve=[this,id] { for(int i=0;i<cards_.size();++i)if(cards_[i].id==id)return i;return -1; };
        QMenu menu;
        menu.addAction("Rename",this,[this,resolve] {
            if (!workspaceAllowed()) return;
            int index=resolve();if(index<0)return;
            for(int row=0;row<library_->count();++row)if(library_->item(row)->data(Qt::UserRole).toInt()==index) { library_->editItem(library_->item(row));break; }
        });
        auto reorder=[this,resolve](int direction) {
            if (!workspaceAllowed()) return;
            int index=resolve();if(index<0)return;
            QVector<int> order;for(int i=0;i<cards_.size();++i)order.append(i);
            std::stable_sort(order.begin(),order.end(),[this](int a,int b){return cards_[a].layer<cards_[b].layer;});
            int at=order.indexOf(index),next=at+direction;
            if(next>=0&&next<order.size())std::swap(order[at],order[next]);
            for(int i=0;i<order.size();++i)cards_[order[i]].layer=i;
            save();
        };
        menu.addAction("Bring forward",this,[reorder]{reorder(1);});
        menu.addAction("Send backward",this,[reorder]{reorder(-1);});
        if(resolve()>=0 && cards_[resolve()].hasPrevious)menu.addAction("Restore previous version",this,[this,resolve] {
            if (!workspaceAllowed()) return;
            int index=resolve();if(index<0 || !cards_[index].hasPrevious)return;
            auto& card=cards_[index];card.name=card.previousName;card.html=card.previousHtml;
            card.contentW=card.previousW;card.contentH=card.previousH;
            card.hasPrevious=false;card.state={};++card.contentVersion;FitCardContent(card);
            save();refreshCards();
        });
        menu.addAction("Delete",this,[this,resolve] {
            if (!workspaceAllowed()) return;
            if(QMessageBox::question(this,"Callayers","Delete this card?")!=QMessageBox::Yes)return;
            if (!workspaceAllowed()) return;
            int index=resolve();if(index<0)return;
            cards_.removeAt(index);selected_=-1;save();refreshCards();
        });
        menu.exec(library_->mapToGlobal(position));
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (!workspaceAllowed()) return;
        if (annotations_.isEmpty() || QMessageBox::question(this, "Callayers", "Clear all annotations?") != QMessageBox::Yes) return;
        if (!workspaceAllowed()) return;
        finishText(false);
        annotations_.clear(); selectedAnnotation_ = -1;
        save();
    });
    connect(preview_, &Preview::pressed, this, &MainWindow::pointerDown);
    connect(preview_, &Preview::moved, this, &MainWindow::pointerMove);
    connect(preview_, &Preview::released, this, [this](QPointF point) { lastPointer_=point; pointerUp(); });
    connect(preview_, &Preview::doubleClicked, this, [this](QPointF logical) {
        int hit = annotationAt(logical);
        if (hit >= 0 && annotations_[hit].kind == Tool::Text) beginText(logical, hit);
    });
    connect(textEdit_, &TextEditorWidget::textChanged, this, [this] {
        if (!editingText_) return;
        textDraft_.text = textEdit_->toPlainText();
        positionTextEditor();
    });
    connect(preview_, &Preview::wheeled, this, [this](QPointF point, int delta) {
        if (workspaceAllowed() && tool_ == Tool::Select && cardAt(point) >= 0) renderer_.scroll(point, delta);
    });
    stack_->addWidget(page);
}

void MainWindow::buildCameraRecovery() {
    recoveryPage_ = new QWidget;
    recoveryPage_->setObjectName("cameraRecovery");
    recoveryPage_->setStyleSheet("QWidget#cameraRecovery {background:#141417; border-radius:8px;}");
    auto layout = new QVBoxLayout(recoveryPage_);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->addStretch();
    auto panel = new QWidget;
    panel->setObjectName("cameraRecoveryPanel");
    panel->setStyleSheet("QWidget#cameraRecoveryPanel {background:transparent;}");
    panel->setFixedWidth(560);
    auto content = new QVBoxLayout(panel);
    content->setSpacing(18);
    recoveryTitle_ = new QLabel;
    recoveryTitle_->setObjectName("cameraRecoveryTitle");
    recoveryTitle_->setWordWrap(true);
    recoveryTitle_->setMinimumHeight(76);
    recoveryTitle_->setStyleSheet("font-size:28px; font-weight:650;");
    recoveryMessage_ = new QLabel;
    recoveryMessage_->setObjectName("cameraRecoveryMessage");
    recoveryMessage_->setWordWrap(true);
    recoveryMessage_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    recoveryMessage_->setStyleSheet("font-size:15px; color:#bbb8c9;");
    auto label = new QLabel("Your webcam");
    recoveryCameras_ = new QComboBox;
    recoveryCameras_->setObjectName("recoveryCameraSelector");
    recoveryCameras_->setAccessibleName("Your webcam");
    recoveryCameras_->setMinimumHeight(42);
    label->setBuddy(recoveryCameras_);
    recoveryRetry_ = new QPushButton("Check again");
    recoveryRetry_->setObjectName("recoveryRetry");
    recoveryRetry_->setMinimumHeight(48);
    recoveryRetry_->setStyleSheet("background:#a39bfa; color:#211c35; font-weight:600; padding:12px;");
    content->addWidget(recoveryTitle_);
    content->addWidget(recoveryMessage_);
    content->addWidget(label);
    content->addWidget(recoveryCameras_);
    content->addWidget(recoveryRetry_);
    auto help = new QLabel("Your cards and annotations will be available when your webcam is working.");
    help->setWordWrap(true);
    help->setStyleSheet("color:#aaa8b6; font-size:12px;");
    content->addWidget(help);
    layout->addWidget(panel, 0, Qt::AlignHCenter);
    layout->addStretch();
    connect(recoveryCameras_, &QComboBox::activated, this, &MainWindow::chooseCamera);
    connect(recoveryRetry_, &QPushButton::clicked, this, &MainWindow::refresh);
    previewStack_->addWidget(recoveryPage_);
}

void MainWindow::setCameraReady(bool ready) {
    if (!ready && cameraReady_) {
        if (auto popup = QApplication::activePopupWidget()) popup->close();
        // Finish the gesture and text draft already in progress before locking
        // controls. Suspension checkpoints card values without clearing them.
        finishText(true);
        pointerUp();
        cancelGeneration();
        renderer_.blur(); cardInput_ = false;
        renderer_.suspend(cards_); save();
    }
    cameraReady_ = ready;
    libraryPanel_->setEnabled(ready);
    annotateButton_->setEnabled(ready);
    annotateBar_->setEnabled(ready);
    if (!ready) {
        output_.stop();
        preview_->setMessage({}, {});
        preview_->setSelection({}, false);
        telemetry_->preview(cards_, false);
    }
    previewStack_->setCurrentWidget(ready ? static_cast<QWidget*>(preview_) : recoveryPage_);
    stack_->setCurrentWidget(sessionAllowed() ? workspacePage_ : stack_->widget(0));
}

void MainWindow::showCameraRecovery(const QString& title, const QString& body) {
    setCameraReady(false);
    recoveryTitle_->setText(title);
    recoveryMessage_->setText(body);
    // An aligned panel inside QStackedWidget does not consistently propagate
    // QLabel's height-for-width on Qt 6.2. Reserve the full wrapped message.
    recoveryMessage_->ensurePolished();
    const auto margins = recoveryMessage_->parentWidget()->layout()->contentsMargins();
    const int width = recoveryMessage_->parentWidget()->width() - margins.left() - margins.right();
    const auto text = recoveryMessage_->fontMetrics().boundingRect(QRect(0, 0, width, 1000), Qt::TextWordWrap, body);
    recoveryMessage_->setMinimumHeight(text.height() + 6);
}

QString MainWindow::cameraMessage(CaptureError error) const {
    if (error == CaptureError::Busy) return "Your webcam is being used by another app. Stop its video or close its camera preview, then choose Retry.";
    if (error == CaptureError::Denied) return "Callayers couldn't get permission to open this webcam. Check your system's camera permissions. If you just installed Callayers, log out and back in, then choose Retry.";
    if (error == CaptureError::Missing) return "Reconnect your webcam and choose Retry, or choose another webcam below.";
    if (error == CaptureError::NoFrames) return "Your webcam isn't sending a picture. Choose Retry, or choose another webcam below.";
    return "Callayers couldn't read your webcam. Close other apps using it, then choose Retry or another webcam.";
}

void MainWindow::refresh() {
    if (authMessage_) authMessage_->setText(auth_.message().isEmpty() ? "Sign in to use Callayers." : auth_.message());
    if (!sessionAllowed()) { setCameraReady(false); finishText(false); cancelGeneration(); renderer_.suspend(cards_); capture_.stop(); outputWanted_ = false; capturePaused_ = false; return; }
    if (offlinePreview_) { setCameraReady(true); camerasBox_->setPlaceholderText("Preview only"); camerasBox_->setEnabled(false); retryCamera_->setEnabled(false); status_->setText("Preview only"); refreshCards(); return; }
    cameras_ = EnumerateCameras();
    auto preferences = store_.preferences();
    const auto identity = wantedCamera_.identity.isEmpty() ? preferences.cameraIdentity : wantedCamera_.identity;
    const auto name = wantedCamera_.identity.isEmpty() ? preferences.cameraName : wantedCamera_.name;
    int match = PopulateCameraSelector(*camerasBox_, cameras_, identity, name);
    PopulateCameraSelector(*recoveryCameras_, cameras_, identity, name);
    recoveryCameras_->setEnabled(!cameras_.isEmpty());
    retryCamera_->setText(identity.isEmpty() ? "Refresh cameras" : "Retry");
    recoveryRetry_->setText(identity.isEmpty() ? "Check again" : "Retry");
    refreshCards();
    if (match >= 0) chooseCamera(match);
    else {
        capture_.stop();
        cameraOpening_.invalidate();
        outputWanted_ = false;
        capturePaused_ = false;
        status_->setText("Choose a webcam to start video");
        if (!identity.isEmpty()) showCameraRecovery("Webcam not connected", cameraMessage(CaptureError::Missing));
        else if (cameras_.isEmpty()) showCameraRecovery("We couldn't find your webcam", "Connect your webcam, check its cable or privacy switch, then choose Check again.");
        else showCameraRecovery("Choose your webcam", "Select a webcam below. Your workspace will open when its live picture is ready.");
    }
}

void MainWindow::refreshCards() {
    library_->blockSignals(true);
    library_->clear();
    QVector<int> order;
    for (int i = 0; i < cards_.size(); ++i) order.push_back(i);
    std::sort(order.begin(), order.end(), [this](int a, int b) { return cards_[a].edited > cards_[b].edited; });
    auto query = search_->text().trimmed();
    for (int index : order) {
        if (!query.isEmpty() && !cards_[index].name.contains(query, Qt::CaseInsensitive)) continue;
        auto item = new QListWidgetItem(cards_[index].name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(cards_[index].visible ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, index);
        item->setToolTip(cards_[index].name);
        library_->addItem(item);
        if (index == selected_) library_->setCurrentItem(item);
    }
    library_->blockSignals(false);
    chatHeading_->setText(selected_ >= 0 && selected_ < cards_.size() ? "Edit “" + cards_[selected_].name + "”" : "New card");
    if (library_->count() == 0) {
        auto empty = new QListWidgetItem(query.isEmpty() ? "Create your first card" : "No matching cards");
        empty->setFlags(Qt::NoItemFlags);
        library_->addItem(empty);
    }
}

void MainWindow::chooseCamera(int index) {
    if (!sessionAllowed() || index < 0 || index >= cameras_.size()) return;
    wantedCamera_ = cameras_[index];
    PopulateCameraSelector(*camerasBox_, cameras_, wantedCamera_.identity, wantedCamera_.name);
    PopulateCameraSelector(*recoveryCameras_, cameras_, wantedCamera_.identity, wantedCamera_.name);
    recoveryCameras_->setEnabled(true);
    showCameraRecovery("Opening your webcam…", wantedCamera_.name);
    cameraOpening_.start();
    capturePaused_ = false;
    preview_->setSelection({}, false);
    preview_->setMessage("Opening your webcam…", cameras_[index].name);
    status_->setText("Opening your webcam…");
    retryCamera_->setText("Retry");
    recoveryRetry_->setText("Retry");
    capture_.start(cameras_[index].identity);
    outputWanted_ = true;
}

void MainWindow::save() {
    renderer_.checkpoint(cards_);
    const auto previous=store_.problem();
    store_.setWorkspace(cards_,annotations_);
    retrySave_->setVisible(!store_.problem().isEmpty());
    if(!store_.problem().isEmpty())operationStatus_->setText(store_.problem());
    else if(!previous.isEmpty()&&operationStatus_->text()==previous)operationStatus_->setText("Workspace saved.");
}

void MainWindow::tick() {
    if (!sessionAllowed()) return;
    bool minimized = windowState().testFlag(Qt::WindowMinimized);
    // Scanning process descriptors can take longer than a video frame. Poll
    // in the background, allowing only one query at a time. Closing the app
    // still performs a fresh check before asking whether to stop video.
    if (!offlinePreview_) {
        if (consumerQuery_.valid() && consumerQuery_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            consumer_ = consumerQuery_.get();
        if (!consumerQuery_.valid() && (!consumerTimer_.isValid() || consumerTimer_.elapsed() >= 1000)) {
            consumerTimer_.start();
            consumerQuery_ = std::async(std::launch::async, [] { return ConsumerName(); });
        }
    }
    const QString consumer = offlinePreview_ ? QString() : consumer_;
    if (minimized && consumer.isEmpty()) {
        telemetry_->preview(cards_, false);
        if (!rendererPaused_) { renderer_.suspend(cards_); save(); rendererPaused_ = true; }
        if (capture_.running()) { capture_.stop(); capturePaused_ = true; }
        return;
    } else if (outputWanted_ && capturePaused_) {
        capturePaused_ = false;
        cameraOpening_.start();
        showCameraRecovery("Opening your webcam…", store_.preferences().cameraName);
        capture_.start(store_.preferences().cameraIdentity);
    }
    rendererPaused_ = false;
    auto error = capture_.error();
    auto webcam = capture_.latest();
    if (error != CaptureError::None) {
        const auto title = error == CaptureError::Missing ? "Webcam not connected" :
            error == CaptureError::Busy ? "Camera in use" :
            error == CaptureError::Denied ? "Camera access blocked" :
            error == CaptureError::NoFrames ? "No picture from your webcam" : "Camera not readable";
        showCameraRecovery(title, cameraMessage(error));
        status_->setText("Camera unavailable — Retry or choose another webcam");
        return;
    }
    if (webcam.isNull() && !offlinePreview_) {
        if (cameraReady_ || (cameraOpening_.isValid() && cameraOpening_.elapsed() >= 5000))
            showCameraRecovery("No picture from your webcam", cameraMessage(CaptureError::NoFrames));
        return;
    }
    if (!cameraReady_) {
        setCameraReady(true);
        // Remember a choice only once it has actually delivered live video.
        auto preferences = store_.preferences();
        preferences.cameraIdentity = wantedCamera_.identity;
        preferences.cameraName = wantedCamera_.name;
        store_.setPreferences(preferences);
        if (outputWanted_) output_.start();
    }
    if (outputWanted_ && !output_.problem().isEmpty()) status_->setText(output_.problem());
    if (!renderer_.render(cards_, annotations_, editingNew_ ? 0 : editingText_)) {
        if (!renderer_.problem().isEmpty()) operationStatus_->setText(renderer_.problem());
        return;
    }
    auto frame = ComposeFrame(webcam, renderer_.image());
    if (checkpointTimer_.elapsed() >= 1000) { if (renderer_.checkpoint(cards_)) save(); checkpointTimer_.restart(); }
    if (editingText_ && !editingNew_) {
        auto privateFrame=ComposeFrame(webcam, renderer_.previewImage());
        preview_->showFrame(privateFrame);
    } else preview_->showFrame(frame);
    telemetry_->preview(cards_, !minimized);
    QRectF selected;
    if (selected_ >= 0 && selected_ < cards_.size() && cards_[selected_].visible) selected = QRectF(cards_[selected_].x, cards_[selected_].y, cards_[selected_].w, cards_[selected_].h);
    if (selectedAnnotation_ >= 0 && selectedAnnotation_ < annotations_.size()) selected = annotations_[selectedAnnotation_].box;
    if (editingText_) selected = textDraft_.box;
    preview_->setSelection(selected, !selected.isNull());
    if (outputWanted_ && (!minimized || !consumer.isEmpty())) output_.publish(frame);
    status_->setText(offlinePreview_ ? "Preview only" : output_.problem().isEmpty() ? (consumer.isEmpty() ? "In your calling app, choose Callayers" : "Sending video to " + consumer) : output_.problem());
}

int MainWindow::cardAt(QPointF logical) const {
    int found = -1, layer = -1;
    for (int i = 0; i < cards_.size(); ++i) {
        if (!cards_[i].visible) continue;
        QRectF rect(cards_[i].x, cards_[i].y, cards_[i].w, cards_[i].h);
        if (rect.contains(logical) && cards_[i].layer >= layer) { found = i; layer = cards_[i].layer; }
    }
    return found;
}

int MainWindow::annotationAt(QPointF logical) const {
    for (int i = annotations_.size() - 1; i >= 0; --i) if (AnnotationHit(annotations_[i], logical)) return i;
    return -1;
}

void MainWindow::selectTool(Tool tool) {
    if (!workspaceAllowed()) return;
    finishText(true); renderer_.blur(); cardInput_ = false; tool_ = tool;
    if (tool != Tool::Select) { selected_ = -1; selectedAnnotation_ = -1; refreshCards(); }
    auto select = findChild<QPushButton*>("toolSelect");
    if (tool == Tool::Select && select) select->setChecked(true);
    preview_->setCursor(tool == Tool::Text ? Qt::IBeamCursor : tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}

void MainWindow::pointerDown(QPointF logical) {
    if (!workspaceAllowed()) return;
    finishText(true); lastPointer_ = logical; dragOrigin_ = logical;
    dragging_ = resizing_ = drawing_ = false; resizeCorner_ = -1;
    if (tool_ != Tool::Select) { cardInput_ = false; renderer_.blur(); }
    if (tool_ == Tool::Eraser) {
        int hit = annotationAt(logical);
        if (hit >= 0) { annotations_.removeAt(hit); selectedAnnotation_ = -1; save(); }
        return;
    }
    if (tool_ == Tool::Text) { beginText(logical, -1); return; }
    if (tool_ != Tool::Select) {
        Annotation mark;
        mark.id = quint64(QDateTime::currentMSecsSinceEpoch()) * 1000;
        for (const auto& existing : annotations_) mark.id = std::max(mark.id, existing.id + 1);
        mark.kind = tool_; mark.color = color_; mark.size = size_;
        mark.points = {logical, logical}; mark.box = QRectF(logical, QSizeF(1, 1));
        annotations_.push_back(mark); drawing_ = true;
        selectedAnnotation_ = annotations_.size() - 1;
        return;
    }
    // Selected handles take precedence, then annotations, then the topmost card.
    if (selectedAnnotation_ >= 0 && selectedAnnotation_ < annotations_.size())
        resizeCorner_ = CornerAt(AnnotationBounds(annotations_[selectedAnnotation_]), logical);
    int annotation = resizeCorner_ >= 0 ? selectedAnnotation_ : annotationAt(logical);
    if (annotation >= 0) {
        renderer_.blur(); cardInput_ = false; selected_ = -1;
        refreshCards();
        selectedAnnotation_ = annotation; annotationOrigin_ = annotations_[annotation];
        resizing_ = resizeCorner_ >= 0; dragging_ = !resizing_;
        return;
    }
    selectedAnnotation_ = -1;
    int card = cardAt(logical);
    if (selected_ >= 0 && selected_ < cards_.size() && cards_[selected_].visible) {
        const auto& c = cards_[selected_];
        resizeCorner_ = CornerAt({c.x,c.y,c.w,c.h}, logical);
        if (resizeCorner_ >= 0) card = selected_;
    }
    selected_ = card;
    if (card < 0) { renderer_.blur(); cardInput_ = false; refreshCards(); return; }
    const auto& c = cards_[card];
    QRectF rect(c.x,c.y,c.w,c.h);
    if (resizeCorner_ < 0) resizeCorner_ = CornerAt(rect, logical);
    resizing_ = resizeCorner_ >= 0;
    dragging_ = !resizing_ && !rect.adjusted(10,10,-10,-10).contains(logical);
    cardOrigin_ = rect.topLeft(); originW_ = c.w; originH_ = c.h;
    cardInput_ = !dragging_ && !resizing_;
    refreshCards();
    if (cardInput_) renderer_.mouse(1, logical); else renderer_.blur();
}

void MainWindow::pointerMove(QPointF logical) {
    if (!workspaceAllowed()) return;
    lastPointer_ = logical;
    if (tool_ == Tool::Eraser) {
        int hit = annotationAt(logical);
        preview_->setHover(hit >= 0 ? AnnotationBounds(annotations_[hit]).adjusted(-4,-4,4,4) : QRectF());
    } else preview_->setHover({});
    if (drawing_ && selectedAnnotation_ >= 0 && selectedAnnotation_ < annotations_.size()) {
        auto& mark = annotations_[selectedAnnotation_];
        if (mark.kind == Tool::Pen) mark.points.push_back(logical); else mark.points.back() = logical;
        mark.box = mark.kind == Tool::Pen ? AnnotationBounds(mark) : QRectF(mark.points.first(), mark.points.back()).normalized();
        return;
    }
    if ((dragging_ || resizing_) && selectedAnnotation_ >= 0 && selectedAnnotation_ < annotations_.size()) {
        QRectF box = AnnotationBounds(annotationOrigin_);
        if (dragging_) {
            auto delta = logical - dragOrigin_;
            box.moveTopLeft({std::clamp(box.x()+delta.x(),0.0,std::max(0.0,CanvasWidth-box.width())),std::clamp(box.y()+delta.y(),0.0,std::max(0.0,CanvasHeight-box.height()))});
        } else box = ResizedBox(box, logical, resizeCorner_, annotationOrigin_.kind == Tool::Text);
        annotations_[selectedAnnotation_] = TransformAnnotation(annotationOrigin_,box);
        return;
    }
    if (selected_ >= 0 && selected_ < cards_.size() && (dragging_ || resizing_)) {
        auto& card = cards_[selected_];
        if (dragging_) {
            card.x=std::clamp(cardOrigin_.x()+logical.x()-dragOrigin_.x(),0.0,std::max(0.0,CanvasWidth-card.w));
            card.y=std::clamp(cardOrigin_.y()+logical.y()-dragOrigin_.y(),0.0,std::max(0.0,CanvasHeight-card.h));
        } else {
            auto box=ResizedBox({cardOrigin_,QSizeF(originW_,originH_)},logical,resizeCorner_,true);
            card.x=box.x();card.y=box.y();card.w=box.width();card.h=box.height();
        }
    } else if (tool_ == Tool::Select) renderer_.mouse(0, logical);
}

void MainWindow::pointerUp() {
    if (cardInput_) { renderer_.mouse(2,lastPointer_); save(); }
    if ((dragging_ || resizing_) && selected_ >= 0 && selected_ < cards_.size()) cards_[selected_].edited=QDateTime::currentMSecsSinceEpoch();
    if (drawing_) telemetry_->content("annotation");
    if (dragging_ || resizing_ || drawing_) save();
    dragging_=resizing_=drawing_=false;
}

void MainWindow::beginText(QPointF logical, int existing) {
    if (!workspaceAllowed()) return;
    finishText(true); renderer_.blur(); cardInput_=false;
    editingNew_=existing<0;
    if (editingNew_) {
        textDraft_={}; textDraft_.id=quint64(QDateTime::currentMSecsSinceEpoch())*1000;
        for (const auto& mark:annotations_) textDraft_.id=std::max(textDraft_.id,mark.id+1);
        textDraft_.kind=Tool::Text; textDraft_.color=color_;
        textDraft_.font=TextSizes[std::clamp(size_,0,2)];
        textDraft_.box={std::min(logical.x(),1200.0),std::min(logical.y(),650.0),std::min(320.0,1280-std::min(logical.x(),1200.0)),textDraft_.font*1.2};
    } else textDraft_=annotations_[existing];
    selectedAnnotation_=existing; selected_=-1;
    editingText_=textDraft_.id; editingOriginal_=textDraft_.text;
    auto rect=preview_->localRect(textDraft_.box);
    textEdit_->setGeometry(rect.adjusted(-8,-8,8,20).toAlignedRect());
    textEdit_->begin(textDraft_,rect.width()/textDraft_.box.width());
    textEdit_->show(); textEdit_->setFocus();
}

void MainWindow::positionTextEditor() {
    if (!editingText_) return;
    textDraft_.box.setHeight(textEdit_->textHeight());
    auto rect=preview_->localRect(textDraft_.box);
    textEdit_->setScale(rect.width()/std::max(1.0,textDraft_.box.width()));
    textEdit_->setGeometry(rect.adjusted(-8,-8,8,16).toAlignedRect());
}

void MainWindow::finishText(bool commit) {
    if (!editingText_) return;
    const auto id=editingText_; editingText_=0;
    textEdit_->finishInput();
    textDraft_.text=textEdit_->toPlainText();
    textDraft_.box.setHeight(textEdit_->textHeight());
    textEdit_->hide();
    if (commit) {
        auto existing=std::find_if(annotations_.begin(),annotations_.end(),[&](const Annotation& a){return a.id==id;});
        if (textDraft_.text.trimmed().isEmpty()) {
            if(existing!=annotations_.end()) annotations_.erase(existing);
            selectedAnnotation_=-1;
        } else if (existing!=annotations_.end()) *existing=textDraft_;
        else { annotations_.push_back(textDraft_); selectedAnnotation_=annotations_.size()-1; telemetry_->content("annotation"); }
        save();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (!workspaceAllowed() && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease ||
        event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::Wheel)) return true;
    if (library_ && watched==library_->viewport() && event->type()==QEvent::MouseButtonRelease) {
        auto mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton && mouse->position().x()>library_->viewport()->width()-26) {
            emit library_->customContextMenuRequested(mouse->position().toPoint());return true;
        }
    }
    if (watched==preview_) {
        if (event->type()==QEvent::Resize) positionTextEditor();
        if ((event->type()==QEvent::KeyPress || event->type()==QEvent::KeyRelease) && cardInput_) {
            renderer_.key(static_cast<QKeyEvent*>(event)); return true;
        }
        if (event->type()==QEvent::KeyPress) {
            auto key=static_cast<QKeyEvent*>(event);
            if (key->key()==Qt::Key_Delete && selectedAnnotation_>=0 && selectedAnnotation_<annotations_.size()) {
                annotations_.removeAt(selectedAnnotation_); selectedAnnotation_=-1;save();return true;
            }
        }
    }
    if (watched==textEdit_ && event->type()==QEvent::FocusOut && editingText_) finishText(true);
    return QMainWindow::eventFilter(watched,event);
}

void MainWindow::importImage() {
    if (!workspaceAllowed()) return;
    auto path = QFileDialog::getOpenFileName(this, "Import image", {}, "Images (*.png *.jpg *.jpeg *.bmp)");
    if (!workspaceAllowed()) return;
    if (path.isEmpty()) return;
    if (cards_.size() >= 16) { operationStatus_->setText("Your library has 16 cards. Delete one before importing another."); return; }
    QImage image(path);
    if (image.isNull()) { operationStatus_->setText("Couldn't read that image. Choose a PNG, JPEG, or BMP."); return; }
    image=image.scaled(QSize(1280,720),Qt::KeepAspectRatio,Qt::SmoothTransformation);
    QString destination=store_.imageDirectory()+"/"+NewId()+".png";
    if(!image.save(destination,"PNG")) { operationStatus_->setText("Couldn't save this image. Retry.");return; }
    Card card;card.id=NewId();card.name=QFileInfo(path).fileName().left(80);card.imagePath=destination;
    card.contentW=image.width();card.contentH=image.height();
    const double scale=std::min({1.0,480.0/card.contentW,360.0/card.contentH});
    card.w=card.contentW*scale;card.h=card.contentH*scale;
    card.edited = QDateTime::currentMSecsSinceEpoch();
    for(const auto& c:cards_)card.layer=std::max(card.layer,c.layer+1);
    cards_.push_front(card); selected_=0;
    telemetry_->content("image_import",card.id);
    save();
    refreshCards();
}

void MainWindow::refreshAttachments() {
    auto layout=attachmentsPanel_->layout();
    while(auto item=layout->takeAt(0)) { if(item->widget())item->widget()->deleteLater();delete item; }
    for(int i=0;i<prompt_->attachments.size();++i) {
        auto source=prompt_->attachments[i].toObject();auto tile=new QWidget;
        auto column=new QVBoxLayout(tile);column->setContentsMargins(4,4,4,4);
        auto name=new QLabel(source["name"].toString());name->setWordWrap(true);
        auto count=new QLabel(QString("%1 characters").arg(source["text"].toString().size()));
        count->setStyleSheet("color:#aaa8b6;font-size:11px;");
        auto show=new QPushButton("Show in text field");auto remove=new QPushButton("×");remove->setAccessibleName("Remove pasted text");
        show->setEnabled(!generating_);remove->setEnabled(!generating_);
        auto actions=new QHBoxLayout;actions->addWidget(show,1);actions->addWidget(remove);
        column->addWidget(name);column->addWidget(count);column->addLayout(actions);layout->addWidget(tile);
        connect(show,&QPushButton::clicked,this,[this,i] { if(generating_||i>=prompt_->attachments.size())return;auto text=prompt_->toPlainText();if(!text.isEmpty())text+="\n\n";text+=prompt_->attachments[i].toObject()["text"].toString();prompt_->attachments.removeAt(i);prompt_->setPlainText(text);prompt_->setFixedHeight(180);refreshAttachments();prompt_->setFocus(); });
        connect(remove,&QPushButton::clicked,this,[this,i] { if(generating_||i>=prompt_->attachments.size())return;prompt_->attachments.removeAt(i);refreshAttachments(); });
    }
    findChild<QScrollArea*>("attachmentScroll")->setVisible(!prompt_->attachments.isEmpty());
}

void MainWindow::cancelGeneration() {
    ++generationEpoch_;
    if (generationReply_) { generationReply_->abort(); generationReply_=nullptr; }
    if (generating_) operationStatus_->setText("Request cancelled. Your card was kept.");
    generating_=false; prompt_->busy=false; refreshAttachments(); cancelGeneration_->hide();
}

void MainWindow::generate() {
    if (!workspaceAllowed()) return;
    if (skipSignIn_) {
        operationStatus_->setText("AI generation requires sign-in. Relaunch without --skip-sign-in.");
        return;
    }
    if (generating_) return;
    if (auth_.token().isEmpty()) { operationStatus_->setText("Sign in to continue creating cards."); return; }
    auto text=prompt_->toPlainText().trimmed();
    if (text.isEmpty() && prompt_->attachments.isEmpty()) return;
    if (prompt_->draftBytes()>100000) { operationStatus_->setText("Please shorten your prompt to 100,000 bytes."); return; }
    const bool revision=selected_>=0 && selected_<cards_.size();
    if (!revision && cards_.size()>=16) { operationStatus_->setText("Your library has 16 cards. Delete one before creating another."); return; }
    const QString targetId=revision?cards_[selected_].id:QString();
    const quint64 version=revision?cards_[selected_].contentVersion:0;
    const quint64 epoch=++generationEpoch_;
    const QJsonArray attachments=prompt_->attachments;
    QJsonObject body{{"requestId",NewId()},{"prompt",text},{"attachments",attachments}};
    if(revision) {
        const auto& c=cards_[selected_];
        QString html=c.html;
        QJsonArray images;
        if (!c.imagePath.isEmpty()) {
            images.append("local");
            if (html.isEmpty()) html="<img src='callayers-image:local' style='width:100%;height:100%;object-fit:contain'>";
        }
        body["currentCard"]=QJsonObject{{"name",c.name},{"html",html},{"width",c.contentW},{"height",c.contentH},{"imageIds",images}};
    }
    QNetworkRequest request(QUrl(auth_.serviceUrl()+"/generate"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    request.setRawHeader("Authorization","Bearer "+auth_.token().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(120000);
    generating_=true; prompt_->busy=true; refreshAttachments(); cancelGeneration_->show();
    operationStatus_->setText(revision?"Editing your card…":"Creating your card…");
    auto reply=network_->post(request,QJsonDocument(body).toJson(QJsonDocument::Compact));
    generationReply_=reply;
    auto deadline=new QTimer(reply); deadline->setSingleShot(true);
    connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort); deadline->start(120000);
    connect(reply,&QNetworkReply::readyRead,this,[reply]{if(reply->bytesAvailable()>1048576)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,targetId,version,epoch,text,attachments] {
        reply->deleteLater();
        if(epoch!=generationEpoch_ || !workspaceAllowed())return;
        generating_=false; prompt_->busy=false; refreshAttachments(); generationReply_=nullptr; cancelGeneration_->hide();
        const auto bytes=reply->readAll();
        QJsonParseError parse; auto document=QJsonDocument::fromJson(bytes,&parse);
        auto payload=document.object();
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(reply->error()!=QNetworkReply::NoError || status!=200 || parse.error!=QJsonParseError::NoError || bytes.size()>1048576) {
            const auto error=payload["error"].toString();
            operationStatus_->setText(error=="daily_limit_reached" ? "You've reached today's generation limit. It resets at midnight UTC." : error=="website_tools_not_ready" ? "Links aren't opened in this version. Paste the page text to create this card." : error=="prompt_too_large" ? "This draft is too large. Use up to 100 KB of text. Your draft was kept." : "Couldn't complete this request. Your existing card was kept. Retrying may use another allowance unit."); return;
        }
        auto result=payload["card"].toObject();
        if(!targetId.isEmpty()) {
            if(!ApplyCardRevision(cards_,targetId,version,result)) { operationStatus_->setText("The card changed or the result was invalid. Your current card was kept.");return; }
        } else {
            Card card;card.id=NewId();card.name=result["name"].toString();card.html=result["html"].toString();
            card.contentW=result["width"].toDouble();card.contentH=result["height"].toDouble();
            if(!ValidCardContent(card) || cards_.size()>=16) { operationStatus_->setText("Couldn't use this card. Your library was kept.");return; }
            FitCardContent(card);card.edited=QDateTime::currentMSecsSinceEpoch();
            for(const auto& c:cards_)card.layer=std::max(card.layer,c.layer+1);
            cards_.push_front(card);selected_=0;
        }
        if(prompt_->toPlainText().trimmed()==text && prompt_->attachments==attachments) { prompt_->clear(); prompt_->attachments={}; refreshAttachments(); }
        telemetry_->content(targetId.isEmpty()?"ai_create":"ai_revision",targetId.isEmpty()?cards_[0].id:targetId);
        save(); refreshCards();
        if(store_.problem().isEmpty())operationStatus_->setText(targetId.isEmpty()?"Card created and saved.":"Card updated and saved.");
    });
}

void MainWindow::closeEvent(QCloseEvent* event) {
    auto consumer = offlinePreview_ ? QString() : ConsumerName();
    if (!consumer.isEmpty() && QMessageBox::question(this, "Callayers", "Quit and stop video for " + consumer + "?") != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    finishText(true);
    cancelGeneration();
    renderer_.suspend(cards_); save();
    telemetry_->preview(cards_,false); telemetry_->flush();
    capture_.stop();
    output_.stop();
    event->accept();
}
