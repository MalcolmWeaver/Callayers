#include "scene_renderer.h"
#include "text_editor_widget.h"
#include <QTimer>
#include <QPainter>
#include <QMouseEvent>
#include <QInputMethodEvent>
#include <QApplication>
#include <QClipboard>
#include "card_validation.h"
#include "video_format.h"
#include <Ultralight/Ultralight.h>
#include <AppCore/Platform.h>
#include <QCoreApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QElapsedTimer>
#include <stdexcept>

namespace {
QString Text(ultralight::String value) { auto s = value.utf8(); return QString::fromUtf8(s.data(), s.length()); }
ultralight::String UL(const QString& value) { return ultralight::String(value.toUtf8().constData()); }
QString Json(const QJsonObject& value) { return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)); }
QString Literal(const QString& value) {
    auto json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
QString Read(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing bundled renderer resources");
    return QString::fromUtf8(f.readAll());
}
class OfflineFiles final : public ultralight::FileSystem {
    QString root_;
    ultralight::FileSystem* delegate_;
    bool allowed(const ultralight::String& path) const {
        QString normalized = QDir::cleanPath(Text(path));
        if (!QDir::isAbsolutePath(normalized)) normalized = QDir(root_).absoluteFilePath(normalized);
        return normalized == root_ + "/resources/icudt67l.dat" || normalized == root_ + "/resources/cacert.pem";
    }
public:
    explicit OfflineFiles(QString root) : root_(std::move(root)), delegate_(ultralight::GetPlatformFileSystem(UL(root_))) {}
    bool FileExists(const ultralight::String& p) override { return allowed(p) && delegate_->FileExists(p); }
    ultralight::String GetFileMimeType(const ultralight::String& p) override { return allowed(p) ? delegate_->GetFileMimeType(p) : "application/unknown"; }
    ultralight::String GetFileCharset(const ultralight::String& p) override { return allowed(p) ? delegate_->GetFileCharset(p) : "utf-8"; }
    ultralight::RefPtr<ultralight::Buffer> OpenFile(const ultralight::String& p) override { return allowed(p) ? delegate_->OpenFile(p) : nullptr; }
};
QString ResourceRoot() {
    auto root = QCoreApplication::applicationDirPath();
    return QFile::exists(root + "/resources/icudt67l.dat") ? root : QDir::cleanPath(root + "/../share/callayers");
}
struct Runtime {
    OfflineFiles files{ResourceRoot()};
    ultralight::RefPtr<ultralight::Renderer> renderer;
    Runtime() {
        auto& platform = ultralight::Platform::instance();
        ultralight::Config config;
        config.memory_profile = ultralight::MemoryProfile::LowMemory;
        config.resource_path_prefix = "resources/";
        platform.set_config(config);
        platform.set_font_loader(ultralight::GetPlatformFontLoader());
        platform.set_file_system(&files);
        platform.set_logger(nullptr);
        renderer = ultralight::Renderer::Create();
    }
};
Runtime& RuntimeInstance() { static Runtime runtime; return runtime; }
QString Document(const Card& card) {
    if (card.imagePath.isEmpty()) return card.html;
    QImage image(card.imagePath);
    if (image.isNull()) throw std::runtime_error("An imported image could not be loaded");
    QByteArray png;
    QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    QString html = card.html.isEmpty() ? "<img alt='Imported image' draggable='false' style='width:100%;height:100%;object-fit:contain' src='callayers-image:local'>" : card.html;
    return html.replace("callayers-image:local", "data:image/png;base64," + QString::fromLatin1(png.toBase64()));
}
}

struct SceneRenderer::Impl {
    ultralight::RefPtr<ultralight::View> view;
    struct Entry { QString id, html, imagePath; quint64 version; bool visible = false, restored = false; };
    QVector<Entry> entries;
    QImage image, previewImage;
    std::unique_ptr<SceneRenderer> ink, privateInk;
    QString problem;
    QByteArray previousScene;
    QElapsedTimer opened;
    bool ready = false;
    QString eval(const QString& script, const QString& frame = {}) {
        ultralight::String error;
        auto value = view->EvaluateScript(UL(script), &error, UL(frame));
        if (!error.empty()) throw std::runtime_error("A card could not restore or save its state");
        return Text(value);
    }
    void initialize() {
        auto& runtime = RuntimeInstance();
        ultralight::ViewConfig config;
        config.is_accelerated = false;
        config.is_transparent = true;
        config.initial_device_scale = double(OutputWidth) / CanvasWidth;
        config.allow_universal_access_from_file_urls = false;
        config.clipboard_read_policy = ultralight::ClipboardReadPolicy::Deny;
        auto session = runtime.renderer->CreateSession(false, "callayers-workspace");
        view = runtime.renderer->CreateView(OutputWidth, OutputHeight, config, session.get());
        auto root = ResourceRoot();
        auto html = Read(root + "/web/overlay.html");
        html.replace("/*CALLAYERS_TEXT_STYLE*/", Read(root + "/web/text-style.css"));
        html.replace("pointer-events:none;transform-origin", "pointer-events:auto;transform-origin");
        view->LoadHTML(UL(html));
        opened.start();
    }
};
SceneRenderer::SceneRenderer() : impl_(std::make_unique<Impl>()) {}
SceneRenderer::~SceneRenderer() = default;
const QImage& SceneRenderer::image() const { return impl_->image; }
const QImage& SceneRenderer::previewImage() const { return impl_->previewImage; }
QString SceneRenderer::problem() const { return impl_->problem; }
QString SceneRenderer::evaluate(const QString& script, const QString& frame) {
    return impl_->view ? impl_->eval(script, frame) : QString();
}
bool SceneRenderer::checkpoint(QVector<Card>& cards) {
    auto& p = *impl_; bool changed = false;
    if (!p.ready) return false;
    try {
        for (int i = 0; i < p.entries.size(); ++i) {
            const auto& entry = p.entries[i];
            if (!entry.visible || !entry.restored) continue;
            auto target = std::find_if(cards.begin(), cards.end(), [&](const Card& c) { return c.id == entry.id && c.contentVersion == entry.version && c.html == entry.html; });
            if (target == cards.end()) continue;
            auto value = p.eval("window.CallayersCard && typeof window.CallayersCard.snapshot==='function' ? JSON.stringify(window.CallayersCard.snapshot()) : '{}'", "card-" + QString::number(i));
            QJsonParseError error;
            auto document = QJsonDocument::fromJson(value.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !document.isObject() || !ValidCardState(document.object())) {
                p.problem = "Couldn't save this card's values. Retry."; continue;
            }
            if (target->state != document.object()) { target->state = document.object(); changed = true; }
        }
    } catch (const std::exception& e) { p.problem = e.what(); }
    return changed;
}
void SceneRenderer::suspend(QVector<Card>& cards) {
    checkpoint(cards);
    auto& p = *impl_;
    if (p.ready) { p.eval("window.stopScene()"); p.entries.clear(); p.previousScene.clear(); p.image = {}; p.previewImage = {}; p.ink.reset(); p.privateInk.reset(); }
}
bool SceneRenderer::renderScene(QVector<Card>& cards, const QVector<Annotation>& annotations) {
    auto& p = *impl_;
    try {
        if (!p.view) p.initialize();
        auto& runtime = RuntimeInstance();
        runtime.renderer->Update();
        if (!p.ready) {
            if (p.view->is_loading()) {
                if (p.opened.elapsed() > 15000) p.problem = "The card renderer could not start. Reopen Callayers.";
                return false;
            }
            if (p.eval("typeof window.setScene") != "function") return false;
            p.eval("documents.length=0");
            p.ready = true;
        }
        bool rebuild = p.entries.size() != cards.size();
        for (int i = 0; !rebuild && i < cards.size(); ++i)
            rebuild = p.entries[i].id != cards[i].id || p.entries[i].html != cards[i].html || p.entries[i].imagePath != cards[i].imagePath || p.entries[i].version != cards[i].contentVersion;
        if (rebuild) {
            checkpoint(cards);
            p.eval("window.stopScene();documents.length=0;");
            p.entries.clear(); p.previousScene.clear();
            for (int i = 0; i < cards.size(); ++i) {
                const auto& c = cards[i];
                p.eval("documents[" + QString::number(i) + "]=" + Literal(Document(c)));
                p.entries.push_back({c.id, c.html, c.imagePath, c.contentVersion});
            }
        }
        QJsonArray cardList, ink;
        for (int i = 0; i < cards.size(); ++i) {
            auto& entry = p.entries[i]; auto& c = cards[i];
            if (entry.visible && !c.visible) { checkpoint(cards); entry.restored = false; }
            entry.visible = c.visible;
            cardList.append(QJsonObject{{"id", i}, {"visible", c.visible}, {"x", c.x}, {"y", c.y}, {"w", c.w}, {"h", c.h}, {"contentW", c.contentW}, {"contentH", c.contentH}, {"layer", c.layer}});
        }
        for (const auto& a : annotations) {
            QJsonArray points;
            for (auto point : a.points) points.append(QJsonArray{point.x(), point.y()});
            ink.append(QJsonObject{{"id", double(a.id)}, {"kind", int(a.kind)}, {"color", a.color}, {"size", a.size}, {"font", a.font}, {"text", a.text}, {"x", a.box.x()}, {"y", a.box.y()}, {"w", a.box.width()}, {"h", a.box.height()}, {"points", points}});
        }
        auto scene = QJsonDocument(QJsonObject{{"cards", cardList}, {"annotations", ink}}).toJson(QJsonDocument::Compact);
        if (scene != p.previousScene) { p.eval("window.setScene(" + QString::fromUtf8(scene) + ")"); p.previousScene = scene; }
        runtime.renderer->Update();
        bool restored = true;
        for (int i = 0; i < cards.size(); ++i) {
            auto& entry = p.entries[i];
            if (!entry.visible || entry.restored) continue;
            const auto frame = "card-" + QString::number(i);
            if (p.eval("document.readyState", frame) != "complete") { restored = false; continue; }
            p.eval("if(window.CallayersCard && typeof window.CallayersCard.restore==='function')window.CallayersCard.restore(" + Json(cards[i].state) + ")", frame);
            entry.restored = true;
        }
        if (!restored) return false;
        runtime.renderer->RefreshDisplay(0);
        runtime.renderer->Render();
        auto surface = p.view->surface();
        if (!surface) return false;
        auto pixels = static_cast<const uchar*>(surface->LockPixels());
        if (!pixels) return false;
        p.image = QImage(pixels, surface->width(), surface->height(), surface->row_bytes(), QImage::Format_ARGB32_Premultiplied).copy();
        surface->UnlockPixels();
        return true;
    } catch (const std::exception& e) { p.problem = e.what(); return false; }
}
bool SceneRenderer::render(QVector<Card>& cards, const QVector<Annotation>& annotations, quint64 privateTextId) {
    auto& p = *impl_;
    if (!renderScene(cards, {})) return false;
    const auto cardsImage = p.image;
    p.previewImage = cardsImage;
    if (annotations.isEmpty()) return true;
    // Keep annotation surfaces independent. DOM changes are frame-paced by the
    // renderer, so hiding and restoring text between two Render calls can reuse
    // stale pixels. Separate layers also keep private drafts out of saved ink.
    if (!p.ink) p.ink = std::make_unique<SceneRenderer>();
    QVector<Card> empty;
    if (!p.ink->renderScene(empty, annotations)) { p.problem=p.ink->problem(); return false; }
    { QPainter painter(&p.image); painter.drawImage(0, 0, p.ink->image()); }
    if (!privateTextId) { p.previewImage = p.image; p.privateInk.reset(); return true; }
    if (!p.privateInk) p.privateInk = std::make_unique<SceneRenderer>();
    QVector<Annotation> privateAnnotations;
    for (const auto& annotation : annotations)
        if (annotation.id != privateTextId) privateAnnotations.push_back(annotation);
    if (!p.privateInk->renderScene(empty, privateAnnotations)) { p.problem=p.privateInk->problem(); return false; }
    { QPainter painter(&p.previewImage); painter.drawImage(0, 0, p.privateInk->image()); }
    return true;
}
void SceneRenderer::mouse(int type, QPointF point) {
    if (!impl_->ready) return;
    if (type == 1) impl_->view->Focus();
    impl_->view->FireMouseEvent({static_cast<ultralight::MouseEvent::Type>(type), int(point.x()), int(point.y()), type == 0 ? ultralight::MouseEvent::kButton_None : ultralight::MouseEvent::kButton_Left});
}
void SceneRenderer::scroll(QPointF point, int delta) {
    if (impl_->ready) impl_->view->FireScrollEvent({ultralight::ScrollEvent::kType_ScrollByPixel, 0, delta, int(point.x()), int(point.y())});
}
void SceneRenderer::blur() { if (impl_->view) impl_->view->Unfocus(); }
static void FireKey(ultralight::View* view, QKeyEvent* event) {
    ultralight::KeyEvent key;
    key.type = event->type() == QEvent::KeyRelease ? ultralight::KeyEvent::kType_KeyUp : ultralight::KeyEvent::kType_RawKeyDown;
    key.modifiers = (event->modifiers().testFlag(Qt::ShiftModifier) ? ultralight::KeyEvent::kMod_ShiftKey : 0) |
        (event->modifiers().testFlag(Qt::ControlModifier) ? ultralight::KeyEvent::kMod_CtrlKey : 0) |
        (event->modifiers().testFlag(Qt::AltModifier) ? ultralight::KeyEvent::kMod_AltKey : 0) |
        (event->modifiers().testFlag(Qt::MetaModifier) ? ultralight::KeyEvent::kMod_MetaKey : 0);
    int code = event->key();
    switch (code) {
    case Qt::Key_Backspace: code=8; break; case Qt::Key_Tab: code=9; break;
    case Qt::Key_Return: case Qt::Key_Enter: code=13; break; case Qt::Key_Escape: code=27; break;
    case Qt::Key_Left: code=37; break; case Qt::Key_Up: code=38; break; case Qt::Key_Right: code=39; break; case Qt::Key_Down: code=40; break;
    case Qt::Key_Home: code=36; break; case Qt::Key_End: code=35; break; case Qt::Key_Delete: code=46; break;
    case Qt::Key_PageUp: code=33; break; case Qt::Key_PageDown: code=34; break;
    case Qt::Key_Shift: code=16; break; case Qt::Key_Control: code=17; break; case Qt::Key_Alt: code=18; break;
    default: if (code > 255) code=0; break;
    }
    key.virtual_key_code = code; key.native_key_code = event->nativeScanCode();
    key.is_auto_repeat = event->isAutoRepeat(); key.is_system_key = false;
    key.is_keypad = event->modifiers().testFlag(Qt::KeypadModifier);
    ultralight::GetKeyIdentifierFromVirtualKeyCode(code, key.key_identifier);
    view->FireKeyEvent(key);
    if (event->type() == QEvent::KeyPress && !event->text().isEmpty() &&
        !event->modifiers().testFlag(Qt::ControlModifier) && !event->modifiers().testFlag(Qt::AltModifier)) {
        key.type = ultralight::KeyEvent::kType_Char;
        key.text = UL(event->text()); key.unmodified_text = key.text;
        view->FireKeyEvent(key);
    }
}

void SceneRenderer::key(QKeyEvent* event) { if(impl_->ready) FireKey(impl_->view.get(),event); }

struct TextEditorWidget::Impl {
    ultralight::RefPtr<ultralight::View> view;
    QTimer* timer=nullptr;
    QImage image;
    Annotation annotation;
    QString text;
    double height=36, scale=1;
    bool pending=false, ready=false;
    QString eval(const QString& script) { ultralight::String error; return Text(view->EvaluateScript(UL(script),&error)); }
};
TextEditorWidget::TextEditorWidget(QWidget* parent):QWidget(parent),impl_(std::make_unique<Impl>()) {
    setAttribute(Qt::WA_TranslucentBackground); setAttribute(Qt::WA_InputMethodEnabled);
    setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
    impl_->timer=new QTimer(this); impl_->timer->setInterval(16);
    connect(impl_->timer,&QTimer::timeout,this,&TextEditorWidget::updateView);
}
TextEditorWidget::~TextEditorWidget()=default;
bool TextEditorWidget::ready() const { return impl_->ready; }
QString TextEditorWidget::toPlainText() const { return impl_->text; }
double TextEditorWidget::textHeight() const { return impl_->height; }
void TextEditorWidget::begin(const Annotation& text,double scale) {
    auto& p=*impl_;p.annotation=text;p.text=text.text;p.height=text.box.height();p.scale=scale;p.pending=true;p.ready=false;
    p.timer->start();updateView();
}
void TextEditorWidget::setScale(double scale) {
    auto& p=*impl_; if(qFuzzyCompare(scale,p.scale))return; p.scale=scale;
    if(p.ready)p.eval(QString("format.scale=%1;surface.style.transform='scale(%1)';report()").arg(scale));
}
void TextEditorWidget::finishInput() { readState();impl_->timer->stop();if(impl_->view)impl_->view->Unfocus(); }
void TextEditorWidget::updateView() {
    auto& p=*impl_;
    if(!isVisible()&&!p.pending)return;
    auto& runtime=RuntimeInstance();
    if(!p.view) {
        ultralight::ViewConfig config;config.is_accelerated=false;config.is_transparent=true;
        config.allow_universal_access_from_file_urls=false;
        config.clipboard_read_policy=ultralight::ClipboardReadPolicy::Deny;
        p.view=runtime.renderer->CreateView(std::max(1,width()),std::max(1,height()),config,nullptr);
        auto html=Read(ResourceRoot()+"/web/text-editor.html");
        html.replace("/*CALLAYERS_TEXT_STYLE*/",Read(ResourceRoot()+"/web/text-style.css"));
        p.view->LoadHTML(UL(html));
    }
    if(int(p.view->width())!=width()||int(p.view->height())!=height())p.view->Resize(std::max(1,width()),std::max(1,height()));
    runtime.renderer->Update();
    if(p.pending&&!p.view->is_loading()&&p.eval("typeof window.beginTextEdit")=="function") {
        p.eval("window.textEditState.finish='';window.beginTextEdit("+Json({{"text",p.text},{"font",p.annotation.font},{"color",p.annotation.color},{"width",p.annotation.box.width()},{"scale",p.scale},{"inset",8},{"dpi",1}})+")");
        p.view->Focus();p.ready=true;p.pending=false;
    }
    if(!p.ready)return;
    readState();
    runtime.renderer->RefreshDisplay(0);runtime.renderer->Render();
    auto surface=p.view->surface();if(!surface)return;
    auto pixels=static_cast<const uchar*>(surface->LockPixels());
    if(pixels){p.image=QImage(pixels,surface->width(),surface->height(),surface->row_bytes(),QImage::Format_ARGB32_Premultiplied).copy();surface->UnlockPixels();}
    update();
}
void TextEditorWidget::readState() {
    auto& p=*impl_;if(!p.ready)return;
    auto state=QJsonDocument::fromJson(p.eval("JSON.stringify(window.textEditState)").toUtf8()).object();
    const auto text=state["text"].toString();const double height=state["height"].toDouble(p.height);
    if(p.text!=text||p.height!=height){p.text=text;p.height=height;emit textChanged();}
    const auto finish=state["finish"].toString();
    if(!finish.isEmpty()){p.eval("window.textEditState.finish=''");emit finished(finish=="commit");}
}
void TextEditorWidget::paintEvent(QPaintEvent*) { QPainter painter(this);painter.drawImage(0,0,impl_->image); }
void TextEditorWidget::keyPressEvent(QKeyEvent* event) {
    auto& p=*impl_;if(!p.ready)return;
    if(event->modifiers().testFlag(Qt::ControlModifier)&&(event->key()==Qt::Key_V||event->key()==Qt::Key_C||event->key()==Qt::Key_X)) {
        if(event->key()==Qt::Key_V) {
            auto clip=QApplication::clipboard()->text();
            p.eval("document.execCommand('insertText',false,"+Literal(clip.left(2000))+ ");report()");
        } else {
            auto selected=p.eval("text.value.substring(text.selectionStart,text.selectionEnd)");
            if(!selected.isEmpty())QApplication::clipboard()->setText(selected);
            if(event->key()==Qt::Key_X)p.eval("document.execCommand('delete');report()");
        }
    } else FireKey(p.view.get(),event);
    readState();event->accept();
}
void TextEditorWidget::keyReleaseEvent(QKeyEvent* event) {if(impl_->ready)FireKey(impl_->view.get(),event);event->accept();}
void TextEditorWidget::mousePressEvent(QMouseEvent* e) {setFocus();if(impl_->ready)impl_->view->FireMouseEvent({ultralight::MouseEvent::kType_MouseDown,int(e->position().x()),int(e->position().y()),ultralight::MouseEvent::kButton_Left});}
void TextEditorWidget::mouseMoveEvent(QMouseEvent* e) {if(impl_->ready)impl_->view->FireMouseEvent({ultralight::MouseEvent::kType_MouseMoved,int(e->position().x()),int(e->position().y()),ultralight::MouseEvent::kButton_None});}
void TextEditorWidget::mouseReleaseEvent(QMouseEvent* e) {if(impl_->ready)impl_->view->FireMouseEvent({ultralight::MouseEvent::kType_MouseUp,int(e->position().x()),int(e->position().y()),ultralight::MouseEvent::kButton_Left});}
void TextEditorWidget::inputMethodEvent(QInputMethodEvent* e) {
    if(impl_->ready&&!e->commitString().isEmpty()){impl_->eval("document.execCommand('insertText',false,"+Literal(e->commitString())+");report()");readState();}e->accept();
}
QVariant TextEditorWidget::inputMethodQuery(Qt::InputMethodQuery query) const {
    if(query==Qt::ImEnabled)return true;
    if(query==Qt::ImSurroundingText)return impl_->text;
    if(query==Qt::ImCursorRectangle)return QRect(8,8,1,30);
    return QWidget::inputMethodQuery(query);
}
