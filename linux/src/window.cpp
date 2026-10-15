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

bool MainWindow::workspaceAllowed() const {
    return skipSignIn_ || auth_.allowed();
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
    previewColumn->addWidget(preview_, 1);
    annotateBar_->setVisible(store_.preferences().annotateOpen);
    annotate->setCheckable(true);
    annotate->setChecked(store_.preferences().annotateOpen);
    body->addWidget(side);
    body->addLayout(previewColumn, 1);
    column->addLayout(body, 1);
    connect(annotate, &QPushButton::clicked, this, [this] {
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
    connect(create, &QPushButton::clicked, this, [this] { selected_ = -1; library_->clearSelection(); chatHeading_->setText("New card"); prompt_->setFocus(); });
    connect(import, &QPushButton::clicked, this, &MainWindow::importImage);
    connect(hide, &QPushButton::clicked, this, [this] { for (auto& card : cards_) card.visible = false; save(); refreshCards(); });
    connect(send, &QPushButton::clicked, this, &MainWindow::generate);
    connect(search_, &QLineEdit::textChanged, this, &MainWindow::refreshCards);
    connect(library_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
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
        auto item = library_->itemAt(position);
        if (!item || !item->data(Qt::UserRole).isValid()) return;
        const QString id=cards_.value(item->data(Qt::UserRole).toInt()).id;
        auto resolve=[this,id] { for(int i=0;i<cards_.size();++i)if(cards_[i].id==id)return i;return -1; };
        QMenu menu;
        menu.addAction("Rename",this,[this,resolve] {
            int index=resolve();if(index<0)return;
            for(int row=0;row<library_->count();++row)if(library_->item(row)->data(Qt::UserRole).toInt()==index) { library_->editItem(library_->item(row));break; }
        });
        auto reorder=[this,resolve](int direction) {
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
            int index=resolve();if(index<0 || !cards_[index].hasPrevious)return;
            auto& card=cards_[index];card.name=card.previousName;card.html=card.previousHtml;
            card.contentW=card.previousW;card.contentH=card.previousH;
            card.hasPrevious=false;card.state={};++card.contentVersion;FitCardContent(card);
            save();refreshCards();
        });
        menu.addAction("Delete",this,[this,resolve] {
            if(QMessageBox::question(this,"Callayers","Delete this card?")!=QMessageBox::Yes)return;
            int index=resolve();if(index<0)return;
            cards_.removeAt(index);selected_=-1;save();refreshCards();
        });
        menu.exec(library_->mapToGlobal(position));
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (annotations_.isEmpty() || QMessageBox::question(this, "Callayers", "Clear all annotations?") != QMessageBox::Yes) return;
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
        if (tool_ == Tool::Select && cardAt(point) >= 0) renderer_.scroll(point, delta);
    });
    stack_->addWidget(page);
}

QString MainWindow::cameraMessage(CaptureError error) const {
    if (error == CaptureError::Busy) return "Camera in use.\nYour webcam is being used by another app. Close that app's camera preview or stop its video, then Retry.";
    if (error == CaptureError::Denied) return "Camera access blocked.\nAllow this user to open the webcam, then Retry.";
    if (error == CaptureError::Missing) return "Your saved webcam isn't connected.\nReconnect it and Retry, or choose another camera.";
    return "Camera not readable.\nClose other apps using your webcam, then Retry.";
}

void MainWindow::refresh() {
    stack_->setCurrentIndex(workspaceAllowed() ? 1 : 0);
    if (authMessage_) authMessage_->setText(auth_.message().isEmpty() ? "Sign in to use Callayers." : auth_.message());
    if (!workspaceAllowed()) { finishText(false); cancelGeneration(); renderer_.suspend(cards_); capture_.stop(); output_.stop(); outputWanted_ = false; capturePaused_ = false; return; }
    if (offlinePreview_) { camerasBox_->setPlaceholderText("Preview only"); camerasBox_->setEnabled(false); retryCamera_->setEnabled(false); status_->setText("Preview only"); refreshCards(); return; }
    cameras_ = EnumerateCameras();
    auto preferences = store_.preferences();
    int match = PopulateCameraSelector(*camerasBox_, cameras_, preferences.cameraIdentity, preferences.cameraName);
    retryCamera_->setText(preferences.cameraIdentity.isEmpty() ? "Refresh cameras" : "Retry");
    refreshCards();
    if (match >= 0) chooseCamera(match);
    else {
        capture_.stop();
        outputWanted_ = false;
        capturePaused_ = false;
        status_->setText("Choose a webcam to start video");
        if (!preferences.cameraIdentity.isEmpty()) preview_->setMessage("Your saved webcam isn't connected.", "Reconnect it and Retry, or choose another camera.");
        else preview_->setMessage("Choose your webcam", cameras_.isEmpty() ? "Connect a webcam, then click Refresh cameras." : "Your camera and cards will appear here");
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
    if (!workspaceAllowed() || index < 0 || index >= cameras_.size()) return;
    auto preferences = store_.preferences();
    preferences.cameraIdentity = cameras_[index].identity;
    preferences.cameraName = cameras_[index].name;
    store_.setPreferences(preferences);
    capturePaused_ = false;
    preview_->setSelection({}, false);
    preview_->setMessage("Opening your webcam…", cameras_[index].name);
    status_->setText("Opening your webcam…");
    retryCamera_->setText("Retry");
    capture_.start(cameras_[index].identity);
    outputWanted_ = true;
    if (!output_.active()) output_.start();
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
    if (!workspaceAllowed()) return;
    bool minimized = windowState().testFlag(Qt::WindowMinimized);
    QString consumer = offlinePreview_ ? QString() : ConsumerName();
    if (minimized && consumer.isEmpty()) {
        telemetry_->preview(cards_, false);
        if (!rendererPaused_) { renderer_.suspend(cards_); save(); rendererPaused_ = true; }
        if (capture_.running()) { capture_.stop(); capturePaused_ = true; }
        return;
    } else if (outputWanted_ && capturePaused_) {
        capturePaused_ = false;
        capture_.start(store_.preferences().cameraIdentity);
    }
    rendererPaused_ = false;
    auto error = capture_.error();
    auto webcam = capture_.latest();
    if (error != CaptureError::None) {
        telemetry_->preview(cards_, false);
        preview_->setSelection({}, false);
        preview_->setMessage(error == CaptureError::Missing ? "Your saved webcam isn't connected." : "Camera", cameraMessage(error));
        status_->setText("Camera unavailable — Retry or choose another webcam");
        return;
    }
    // Report output setup failures even before the first webcam frame arrives.
    if (outputWanted_ && !output_.problem().isEmpty()) status_->setText(output_.problem());
    if (webcam.isNull() && !offlinePreview_) return;
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
    finishText(true); renderer_.blur(); cardInput_ = false; tool_ = tool;
    if (tool != Tool::Select) { selected_ = -1; selectedAnnotation_ = -1; refreshCards(); }
    auto select = findChild<QPushButton*>("toolSelect");
    if (tool == Tool::Select && select) select->setChecked(true);
    preview_->setCursor(tool == Tool::Text ? Qt::IBeamCursor : tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}

void MainWindow::pointerDown(QPointF logical) {
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
    auto path = QFileDialog::getOpenFileName(this, "Import image", {}, "Images (*.png *.jpg *.jpeg *.bmp)");
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
