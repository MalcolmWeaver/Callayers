#include "window.h"
#include "compose.h"
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
QString NewId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

MainWindow::MainWindow() : store_(), auth_(this) {
    setWindowTitle("Callayers");
    resize(1280, 800);
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
    auto file = menuBar()->addMenu("File");
    file->addAction("Sign out", this, &MainWindow::signOut);
    file->addAction("Quit", this, &QWidget::close);
    auth_.restore();
    refresh();
}

void MainWindow::signOut() { auth_.signOut(); output_.stop(); capture_.stop(); outputWanted_ = false; }

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
    auto header = new QHBoxLayout;
    auto mark = new QLabel("Callayers");
    mark->setStyleSheet("font-weight:650; padding:0 8px;");
    camerasBox_ = new QComboBox;
    status_ = new QLabel("In your calling app, choose Callayers");
    auto annotate = new QPushButton("Annotate");
    header->addWidget(mark);
    header->addWidget(annotate);
    header->addWidget(camerasBox_, 1);
    header->addWidget(status_);
    column->addLayout(header);
    auto body = new QHBoxLayout;
    auto sidebar = new QVBoxLayout;
    auto create = new QPushButton("Create with AI");
    auto import = new QPushButton("Import image");
    auto hide = new QPushButton("Hide all");
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search cards");
    library_ = new QListWidget;
    chatHeading_ = new QLabel("New card");
    prompt_ = new QPlainTextEdit;
    prompt_->setPlaceholderText("Describe the card");
    prompt_->setFixedHeight(90);
    auto send = new QPushButton("Send");
    sidebar->addWidget(create);
    sidebar->addWidget(import);
    sidebar->addWidget(hide);
    sidebar->addWidget(search_);
    sidebar->addWidget(library_, 1);
    sidebar->addWidget(chatHeading_);
    sidebar->addWidget(prompt_);
    sidebar->addWidget(send);
    auto side = new QWidget;
    side->setLayout(sidebar);
    side->setFixedWidth(320);
    preview_ = new Preview;
    textEdit_ = new QPlainTextEdit(preview_);
    textEdit_->hide();
    auto commitText = new QShortcut(QKeySequence("Ctrl+Return"), textEdit_);
    auto cancelText = new QShortcut(QKeySequence("Escape"), textEdit_);
    connect(commitText, &QShortcut::activated, this, [this] { finishText(true); });
    connect(cancelText, &QShortcut::activated, this, [this] { finishText(false); });
    annotateBar_ = new QWidget;
    auto tools = new QHBoxLayout(annotateBar_);
    const QList<QPair<QString, Tool>> names{{"Select", Tool::Select}, {"Pen", Tool::Pen}, {"Rectangle", Tool::Rectangle}, {"Ellipse", Tool::Ellipse}, {"Line", Tool::Line}, {"Arrow", Tool::Arrow}, {"Text", Tool::Text}, {"Eraser", Tool::Eraser}};
    for (auto [label, value] : names) {
        auto button = new QPushButton(label);
        connect(button, &QPushButton::clicked, this, [this, value] { tool_ = value; });
        tools->addWidget(button);
    }
    for (auto swatch : {"#ffd166", "#f87171", "#67e8f9", "#86efac", "#ffffff", "#111111"}) {
        auto button = new QPushButton;
        button->setFixedSize(22, 22);
        button->setStyleSheet(QString("background:%1; border-radius:11px;").arg(swatch));
        connect(button, &QPushButton::clicked, this, [this, swatch] { color_ = swatch; });
        tools->addWidget(button);
    }
    auto clear = new QPushButton("Clear annotations");
    tools->addWidget(clear);
    auto previewColumn = new QVBoxLayout;
    previewColumn->addWidget(preview_, 1);
    previewColumn->addWidget(annotateBar_);
    annotateBar_->setVisible(store_.preferences().annotateOpen);
    body->addWidget(side);
    body->addLayout(previewColumn, 1);
    column->addLayout(body, 1);
    connect(annotate, &QPushButton::clicked, this, [this] {
        annotateBar_->setVisible(!annotateBar_->isVisible());
        auto preferences = store_.preferences();
        preferences.annotateOpen = annotateBar_->isVisible();
        store_.setPreferences(preferences);
    });
    connect(camerasBox_, &QComboBox::currentIndexChanged, this, &MainWindow::chooseCamera);
    connect(create, &QPushButton::clicked, this, [this] { selected_ = -1; chatHeading_->setText("New card"); prompt_->setFocus(); });
    connect(import, &QPushButton::clicked, this, &MainWindow::importImage);
    connect(hide, &QPushButton::clicked, this, [this] { for (auto& card : cards_) card.visible = false; save(); refreshCards(); });
    connect(send, &QPushButton::clicked, this, &MainWindow::generate);
    connect(search_, &QLineEdit::textChanged, this, &MainWindow::refreshCards);
    connect(library_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        int index = item->data(Qt::UserRole).toInt();
        if (index < 0 || index >= cards_.size()) return;
        cards_[index].visible = item->checkState() == Qt::Checked;
        cards_[index].name = item->text();
        cards_[index].edited = QDateTime::currentMSecsSinceEpoch();
        save();
    });
    connect(library_, &QListWidget::currentRowChanged, this, [this](int row) {
        auto item = library_->item(row);
        selected_ = item ? item->data(Qt::UserRole).toInt() : -1;
        chatHeading_->setText(selected_ >= 0 ? "Edit “" + cards_.value(selected_).name + "”" : "New card");
    });
    library_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(library_, &QListWidget::customContextMenuRequested, this, [this](QPoint position) {
        auto item = library_->itemAt(position);
        if (!item) return;
        int index = item->data(Qt::UserRole).toInt();
        QMenu menu;
        menu.addAction("Rename", this, [this, item] { library_->editItem(item); });
        menu.addAction("Bring forward", this, [this, index] { if (index >= 0) { cards_[index].layer += 1; save(); } });
        menu.addAction("Send backward", this, [this, index] { if (index >= 0) { cards_[index].layer = std::max(0, cards_[index].layer - 1); save(); } });
        if (index >= 0 && cards_[index].hasPrevious) menu.addAction("Restore previous version", this, [this, index] {
            cards_[index].name = cards_[index].previousName;
            cards_[index].html = cards_[index].previousHtml;
            cards_[index].contentW = cards_[index].previousW;
            cards_[index].contentH = cards_[index].previousH;
            cards_[index].hasPrevious = false;
            save();
            refreshCards();
        });
        menu.addAction("Delete", this, [this, index] {
            if (QMessageBox::question(this, "Callayers", "Delete this card?") != QMessageBox::Yes) return;
            if (index >= 0 && index < cards_.size()) cards_.removeAt(index);
            selected_ = -1;
            save();
            refreshCards();
        });
        menu.exec(library_->mapToGlobal(position));
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (annotations_.isEmpty() || QMessageBox::question(this, "Callayers", "Clear all annotations?") != QMessageBox::Yes) return;
        annotations_.clear();
        save();
    });
    connect(preview_, &Preview::pressed, this, &MainWindow::pointerDown);
    connect(preview_, &Preview::moved, this, &MainWindow::pointerMove);
    connect(preview_, &Preview::released, this, [this](QPointF) { pointerUp(); });
    connect(preview_, &Preview::doubleClicked, this, [this](QPointF logical) {
        int hit = annotationAt(logical);
        if (hit >= 0 && annotations_[hit].kind == Tool::Text) beginText(logical, hit);
    });
    connect(textEdit_, &QPlainTextEdit::textChanged, this, [this] {
        if (!editingText_) return;
        for (auto& mark : annotations_) if (mark.id == editingText_) { mark.text = textEdit_->toPlainText(); mark.box.setHeight(std::max(mark.font, textEdit_->document()->size().height())); }
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
    stack_->setCurrentIndex(auth_.allowed() ? 1 : 0);
    if (authMessage_) authMessage_->setText(auth_.message().isEmpty() ? "Sign in to use Callayers." : auth_.message());
    if (!auth_.allowed()) { capture_.stop(); output_.stop(); outputWanted_ = false; return; }
    cameras_ = EnumerateCameras();
    camerasBox_->blockSignals(true);
    camerasBox_->clear();
    int match = -1;
    auto preferences = store_.preferences();
    for (int i = 0; i < cameras_.size(); ++i) {
        camerasBox_->addItem(cameras_[i].name, cameras_[i].identity);
        if (cameras_[i].identity == preferences.cameraIdentity) match = i;
    }
    camerasBox_->blockSignals(false);
    if (match >= 0) camerasBox_->setCurrentIndex(match);
    else if (!preferences.cameraIdentity.isEmpty()) preview_->setMessage("Your saved webcam isn't connected.", "Reconnect it and Retry, or choose another camera.");
    else preview_->setMessage("Choose your webcam", "Your camera and cards will appear here");
    refreshCards();
    if (match >= 0) chooseCamera(match);
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
    }
    library_->blockSignals(false);
    if (library_->count() == 0) {
        auto empty = new QListWidgetItem("Create your first card");
        empty->setFlags(Qt::NoItemFlags);
        library_->addItem(empty);
    }
}

void MainWindow::chooseCamera(int index) {
    if (index < 0 || index >= cameras_.size()) return;
    auto preferences = store_.preferences();
    preferences.cameraIdentity = cameras_[index].identity;
    preferences.cameraName = cameras_[index].name;
    store_.setPreferences(preferences);
    capture_.start(cameras_[index].identity);
    outputWanted_ = true;
    if (!output_.active()) output_.start();
}

void MainWindow::save() { store_.setCards(cards_); store_.setAnnotations(annotations_); }

void MainWindow::tick() {
    if (!auth_.allowed()) return;
    bool minimized = windowState().testFlag(Qt::WindowMinimized);
    QString consumer = ConsumerName();
    if (minimized && consumer.isEmpty()) {
        if (capture_.running()) capture_.stop();
    } else if (outputWanted_ && !capture_.running() && !store_.preferences().cameraIdentity.isEmpty()) {
        capture_.start(store_.preferences().cameraIdentity);
    }
    auto error = capture_.error();
    auto webcam = capture_.latest();
    if (error != CaptureError::None && webcam.isNull()) {
        preview_->setMessage(error == CaptureError::Missing ? "Your saved webcam isn't connected." : "Camera", cameraMessage(error));
        return;
    }
    if (webcam.isNull()) return;
    quint64 hidden = editingText_;
    auto frame = ComposeFrame(webcam, cards_, annotations_, hidden);
    preview_->showFrame(frame);
    QRectF selected;
    if (selected_ >= 0 && selected_ < cards_.size()) selected = QRectF(cards_[selected_].x, cards_[selected_].y, cards_[selected_].w, cards_[selected_].h);
    preview_->setSelection(selected, tool_ == Tool::Select && selected_ >= 0);
    if (outputWanted_ && (!minimized || !consumer.isEmpty())) output_.publish(frame);
    if (minimized && consumer.isEmpty()) capture_.stop();
    else if (outputWanted_ && !capture_.running() && !store_.preferences().cameraIdentity.isEmpty()) capture_.start(store_.preferences().cameraIdentity);
    status_->setText(output_.problem().isEmpty() ? (consumer.isEmpty() ? "In your calling app, choose Callayers" : "Sending video to " + consumer) : output_.problem());
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
    for (int i = annotations_.size() - 1; i >= 0; --i) {
        const auto& mark = annotations_[i];
        if (mark.kind == Tool::Pen || mark.kind == Tool::Line || mark.kind == Tool::Arrow) {
            for (auto point : mark.points) if (QLineF(point, logical).length() <= 12) return i;
        } else if (mark.box.adjusted(-6, -6, 6, 6).contains(logical)) return i;
    }
    return -1;
}

void MainWindow::pointerDown(QPointF logical) {
    if (tool_ == Tool::Eraser) {
        int hit = annotationAt(logical);
        if (hit >= 0) { annotations_.removeAt(hit); save(); }
        return;
    }
    if (tool_ == Tool::Text) { beginText(logical, -1); return; }
    if (tool_ != Tool::Select) {
        Annotation mark;
        mark.id = QDateTime::currentMSecsSinceEpoch();
        mark.kind = tool_;
        mark.color = color_;
        mark.size = size_;
        mark.points = {logical, logical};
        mark.box = QRectF(logical, QSizeF(1, 1));
        annotations_.push_back(mark);
        drawing_ = true;
        selectedAnnotation_ = annotations_.size() - 1;
        return;
    }
    selected_ = cardAt(logical);
    if (selected_ < 0) return;
    QRectF rect(cards_[selected_].x, cards_[selected_].y, cards_[selected_].w, cards_[selected_].h);
    resizing_ = QLineF(rect.bottomRight(), logical).length() < 18;
    dragging_ = !resizing_;
    dragOrigin_ = logical;
    cardOrigin_ = rect.topLeft();
    originW_ = cards_[selected_].w;
    originH_ = cards_[selected_].h;
    refreshCards();
}

void MainWindow::pointerMove(QPointF logical) {
    if (drawing_ && selectedAnnotation_ >= 0) {
        auto& mark = annotations_[selectedAnnotation_];
        if (mark.kind == Tool::Pen) mark.points.push_back(logical);
        else mark.points.back() = logical;
        mark.box = QRectF(mark.points.first(), mark.points.back()).normalized();
        return;
    }
    if (selected_ < 0 || (!dragging_ && !resizing_)) return;
    auto& card = cards_[selected_];
    if (dragging_) {
        card.x = std::clamp(cardOrigin_.x() + logical.x() - dragOrigin_.x(), 0.0, CanvasWidth - card.w);
        card.y = std::clamp(cardOrigin_.y() + logical.y() - dragOrigin_.y(), 0.0, CanvasHeight - card.h);
    } else {
        double factor = std::clamp((logical.x() - card.x) / std::max(1.0, originW_), 0.2, 4.0);
        card.w = std::min(originW_ * factor, CanvasWidth - card.x);
        card.h = std::min(originH_ * factor, CanvasHeight - card.y);
    }
}

void MainWindow::pointerUp() {
    if ((dragging_ || resizing_) && selected_ >= 0) cards_[selected_].edited = QDateTime::currentMSecsSinceEpoch();
    if (dragging_ || resizing_ || drawing_) save();
    dragging_ = resizing_ = drawing_ = false;
}

void MainWindow::beginText(QPointF logical, int existing) {
    if (existing < 0) {
        Annotation mark;
        mark.id = QDateTime::currentMSecsSinceEpoch();
        mark.kind = Tool::Text;
        mark.color = color_;
        mark.font = TextSizes[std::clamp(size_, 0, 2)];
        mark.box = QRectF(logical.x(), logical.y(), 320, mark.font * 1.4);
        annotations_.push_back(mark);
        existing = annotations_.size() - 1;
    }
    editingText_ = annotations_[existing].id;
    editingOriginal_ = annotations_[existing].text;
    textEdit_->setPlainText(annotations_[existing].text);
    textEdit_->setGeometry(40, 40, 360, 120);
    textEdit_->show();
    textEdit_->setFocus();
}

void MainWindow::finishText(bool commit) {
    if (!commit) {
        for (int i = 0; i < annotations_.size(); ++i) if (annotations_[i].id == editingText_) {
            if (editingOriginal_.isEmpty()) annotations_.removeAt(i);
            else annotations_[i].text = editingOriginal_;
        }
    }
    editingText_ = 0;
    textEdit_->hide();
    save();
}

void MainWindow::importImage() {
    auto path = QFileDialog::getOpenFileName(this, "Import image", {}, "Images (*.png *.jpg *.jpeg *.bmp)");
    if (path.isEmpty()) return;
    QImage image(path);
    if (image.isNull()) return;
    QString destination = store_.imageDirectory() + "/" + NewId() + "." + QFileInfo(path).suffix();
    QFile::copy(path, destination);
    Card card;
    card.id = NewId();
    card.name = QFileInfo(path).fileName();
    card.imagePath = destination;
    card.contentW = std::clamp(image.width(), 1, CanvasWidth);
    card.contentH = std::clamp(image.height(), 1, CanvasHeight);
    double fit = std::min(480.0 / card.contentW, 360.0 / card.contentH);
    card.w = card.contentW * fit;
    card.h = card.contentH * fit;
    card.edited = QDateTime::currentMSecsSinceEpoch();
    cards_.push_front(card);
    save();
    refreshCards();
}

void MainWindow::generate() {
    if (generating_ || auth_.token().isEmpty()) { status_->setText("Enter a prompt and sign in to continue."); return; }
    auto text = prompt_->toPlainText().trimmed();
    if (text.isEmpty()) return;
    generating_ = true;
    status_->setText(selected_ >= 0 ? "Editing your card…" : "Creating your card…");
    QJsonObject body;
    body["requestId"] = NewId();
    body["prompt"] = text;
    if (selected_ >= 0 && selected_ < cards_.size()) {
        QJsonObject current;
        current["name"] = cards_[selected_].name;
        current["html"] = cards_[selected_].html;
        current["width"] = cards_[selected_].contentW;
        current["height"] = cards_[selected_].contentH;
        current["imageIds"] = QJsonArray();
        body["currentCard"] = current;
    }
    QNetworkRequest request(QUrl(auth_.serviceUrl() + "/generate"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + auth_.token().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto reply = (new QNetworkAccessManager(this))->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        generating_ = false;
        auto payload = QJsonDocument::fromJson(reply->readAll()).object();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (status != 200) {
            auto error = payload.value("error").toString();
            status_->setText(error == "daily_limit_reached" ? "You've reached today's generation limit. It resets at midnight UTC." : "Couldn't complete this request. Your existing card was kept.");
            return;
        }
        auto data = payload.value("card").toObject();
        if (selected_ >= 0 && selected_ < cards_.size()) {
            cards_[selected_].previousName = cards_[selected_].name;
            cards_[selected_].previousHtml = cards_[selected_].html;
            cards_[selected_].previousW = cards_[selected_].contentW;
            cards_[selected_].previousH = cards_[selected_].contentH;
            cards_[selected_].hasPrevious = true;
            cards_[selected_].name = data.value("name").toString(cards_[selected_].name);
            cards_[selected_].html = data.value("html").toString();
            cards_[selected_].contentW = data.value("width").toDouble(cards_[selected_].contentW);
            cards_[selected_].contentH = data.value("height").toDouble(cards_[selected_].contentH);
            cards_[selected_].edited = QDateTime::currentMSecsSinceEpoch();
        } else {
            Card card;
            card.id = NewId();
            card.name = data.value("name").toString("New card");
            card.html = data.value("html").toString();
            card.contentW = data.value("width").toDouble(480);
            card.contentH = data.value("height").toDouble(270);
            card.w = card.contentW;
            card.h = card.contentH;
            card.edited = QDateTime::currentMSecsSinceEpoch();
            if (card.html.isEmpty() || card.w < 180 || card.h < 100) { status_->setText("Couldn't complete this request. Your existing card was kept."); return; }
            cards_.push_front(card);
            selected_ = 0;
        }
        prompt_->clear();
        save();
        refreshCards();
        status_->setText("Card created. It is shown in your preview and saved cards.");
    });
}

void MainWindow::closeEvent(QCloseEvent* event) {
    auto consumer = ConsumerName();
    if (!consumer.isEmpty() && QMessageBox::question(this, "Callayers", "Quit and stop video for " + consumer + "?") != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    finishText(true);
    capture_.stop();
    output_.stop();
    event->accept();
}
