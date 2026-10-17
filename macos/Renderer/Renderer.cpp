#include "Renderer.h"
#include "SDKCompatibility.h"
#include <AppCore/Platform.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace callayers::mac {
namespace {
std::string Text(const ultralight::String& value) {
    const auto utf8 = value.utf8(); return {utf8.data(), utf8.length()};
}
ultralight::String UL(const std::string& value) { return ultralight::String(value.c_str()); }
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("A bundled renderer resource is missing: " + path.filename().string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::string Quote(const std::string& text) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += char(c);
    }
    return result + '"';
}
class ResourceFiles final : public ultralight::FileSystem {
    std::filesystem::path root_;
    ultralight::FileSystem* delegate_;
    bool allowed(const ultralight::String& value) const {
        auto path = std::filesystem::path(Text(value));
        if (!path.is_absolute()) path = root_ / path;
        path = path.lexically_normal();
        return path == root_ / "resources/icudt67l.dat" || path == root_ / "resources/cacert.pem";
    }
public:
    explicit ResourceFiles(const std::string& root)
        : root_(std::filesystem::canonical(root)), delegate_(ultralight::GetPlatformFileSystem(UL(root_.string()))) {}
    bool FileExists(const ultralight::String& path) override { return allowed(path) && delegate_->FileExists(path); }
    ultralight::String GetFileMimeType(const ultralight::String& path) override {
        return allowed(path) ? delegate_->GetFileMimeType(path) : "application/unknown";
    }
    ultralight::String GetFileCharset(const ultralight::String& path) override {
        return allowed(path) ? delegate_->GetFileCharset(path) : "utf-8";
    }
    ultralight::RefPtr<ultralight::Buffer> OpenFile(const ultralight::String& path) override {
        return allowed(path) ? delegate_->OpenFile(path) : nullptr;
    }
};
struct Runtime {
    const std::string root;
    const std::thread::id owner = std::this_thread::get_id();
    ResourceFiles files;
    PrivateClipboard clipboard;
    ultralight::ViewConfig viewConfig;
    ultralight::RefPtr<ultralight::Renderer> renderer;
    explicit Runtime(const std::string& path) : root(std::filesystem::canonical(path).string()), files(root) {
        ultralight::Config config;
        Configure(config, viewConfig);
        auto& platform = ultralight::Platform::instance();
        platform.set_config(config);
        platform.set_file_system(&files);
        platform.set_font_loader(ultralight::GetPlatformFontLoader());
        platform.set_clipboard(&clipboard);
        renderer = ultralight::Renderer::Create();
    }
    void checkThread() const {
        if (owner != std::this_thread::get_id()) throw std::logic_error("Renderer used outside its UI thread");
    }
};
Runtime& GetRuntime(const std::string& path) {
    static Runtime runtime(path);
    runtime.checkThread();
    if (runtime.root != std::filesystem::canonical(path).string())
        throw std::runtime_error("All renderer instances must use the same bundled resources");
    return runtime;
}
}

struct Renderer::Impl {
    Runtime& runtime;
    ultralight::RefPtr<ultralight::View> view;
    Frame frame;
    std::string problem, pendingHTML, pendingState = "{}";
    bool hostReady = false, hasPending = false, cardReady = false;
    double scale = 1, width = 640, height = 400;
    std::chrono::steady_clock::time_point opened = std::chrono::steady_clock::now();
    explicit Impl(const std::string& root) : runtime(GetRuntime(root)) {
        auto session = runtime.renderer->CreateSession(false, "callayers-renderer-preview");
        view = runtime.renderer->CreateView(1920, 1080, runtime.viewConfig, session.get());
        std::string html = Read(std::filesystem::path(root) / "scene-host.html");
        const std::string marker = "/*CALLAYERS_HOST*/";
        const auto pos = html.find(marker);
        if (pos == std::string::npos) throw std::runtime_error("The renderer host resource is invalid");
        html.replace(pos, marker.size(), Read(std::filesystem::path(root) / "scene-host.js"));
        view->LoadHTML(UL(html));
    }
    std::string eval(const std::string& script) {
        runtime.checkThread();
        ultralight::String error;
        auto result = view->EvaluateScript(UL(script), &error);
        if (!error.empty()) throw std::runtime_error("The card renderer could not complete an operation: " + Text(error));
        return Text(result);
    }
};
Renderer::Renderer(const std::string& root) : impl_(std::make_unique<Impl>(root)) {}
Renderer::~Renderer() = default;
void Renderer::loadCard(std::string html, std::string state, double width, double height) {
    auto& p = *impl_; p.runtime.checkThread();
    if (html.size() > 6 * 1024 * 1024 || state.size() > 16384) throw std::runtime_error("The card or its saved values are too large");
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || width > 1280 || height < 1 || height > 720)
        throw std::runtime_error("Invalid card dimensions");
    p.width = width; p.height = height;
    p.pendingHTML = std::move(html); p.pendingState = std::move(state);
    p.hasPending = true; p.cardReady = false; p.problem.clear();
    p.opened = std::chrono::steady_clock::now();
}
void Renderer::clear() {
    auto& p = *impl_; p.runtime.checkThread();
    p.hasPending = false; p.cardReady = false;
    if (p.hostReady) p.eval("CallayersHost.clear()");
}
bool Renderer::tick() {
    auto& p = *impl_; p.runtime.checkThread();
    if (!p.problem.empty()) return false;
    try {
        p.runtime.renderer->Update();
        if (!p.hostReady && !p.view->is_loading() && p.eval("typeof CallayersHost") == "object") {
            p.eval("CallayersHost.configure(" + Quote(Read(std::filesystem::path(p.runtime.root) / "card-bridge.js")) + ")");
            p.hostReady = true;
        }
        if (p.hostReady && p.hasPending) {
            const auto result = p.eval("CallayersHost.load(" + Quote(p.pendingHTML) + "," + Quote(p.pendingState) + "," + std::to_string(p.width) + "," + std::to_string(p.height) + ")");
            if (result != "ok") throw std::runtime_error(result);
            p.eval("CallayersHost.scale(" + std::to_string(p.scale) + ")");
            p.hasPending = false;
        }
        if (p.hostReady) {
            const auto status = p.eval("CallayersHost.status()");
            if (status.rfind("error:", 0) == 0) throw std::runtime_error(status.substr(6));
            p.cardReady = status == "ready";
            if (status == "loading" && std::chrono::steady_clock::now() - p.opened > std::chrono::seconds(10))
                throw std::runtime_error("The card did not finish loading. Reload the preview to retry.");
        } else if (std::chrono::steady_clock::now() - p.opened > std::chrono::seconds(10)) {
            throw std::runtime_error("The renderer did not finish starting");
        }
        p.runtime.renderer->RefreshDisplay(0);
        p.runtime.renderer->Render();
        auto* surface = p.view->surface();
        if (!surface || surface->dirty_bounds().IsEmpty()) return false;
        const auto* pixels = static_cast<const uint8_t*>(surface->LockPixels());
        if (!pixels) return false;
        p.frame.width = surface->width(); p.frame.height = surface->height(); p.frame.rowBytes = surface->row_bytes();
        const size_t size = size_t(p.frame.rowBytes) * p.frame.height;
        try { p.frame.bgra.assign(pixels, pixels + size); }
        catch (...) { surface->UnlockPixels(); throw; }
        surface->UnlockPixels(); surface->ClearDirtyBounds(); ++p.frame.serial;
        return true;
    } catch (const std::exception& error) { p.problem = error.what(); p.cardReady = false; return false; }
}
bool Renderer::ready() const { return impl_->cardReady; }
const Frame& Renderer::frame() const { return impl_->frame; }
const std::string& Renderer::problem() const { return impl_->problem; }
void Renderer::mouse(int type, int x, int y, int button) {
    auto& p = *impl_; p.runtime.checkThread();
    if (!p.cardReady || type < 0 || type > 2 || button < 0 || button > 3) return;
    if (type == 1) p.view->Focus();
    ultralight::MouseEvent event{};
    event.type = static_cast<ultralight::MouseEvent::Type>(type);
    event.x = x; event.y = y; event.button = static_cast<ultralight::MouseEvent::Button>(button);
    p.view->FireMouseEvent(event);
}
void Renderer::scroll(int x, int y, int dx, int dy) {
    auto& p = *impl_; p.runtime.checkThread();
    if (!p.cardReady) return;
    mouse(0, x, y, 0);
    ultralight::ScrollEvent event{};
    event.type = ultralight::ScrollEvent::kType_ScrollByPixel;
    event.delta_x = dx; event.delta_y = dy;
#if CALLAYERS_ULTRALIGHT_MAJOR == 2
    event.x = x; event.y = y;
#endif
    p.view->FireScrollEvent(event);
}
void Renderer::key(int type, int vk, int nk, unsigned modifiers, const std::string& text,
                   const std::string& unmodified, bool repeat) {
    auto& p = *impl_; p.runtime.checkThread();
    if (!p.cardReady || type < 0 || type > 3) return;
    ultralight::KeyEvent event;
    event.type = static_cast<ultralight::KeyEvent::Type>(type);
    event.virtual_key_code = vk; event.native_key_code = nk; event.modifiers = modifiers;
    event.text = UL(text); event.unmodified_text = UL(unmodified); event.is_auto_repeat = repeat;
    event.is_keypad = false; event.is_system_key = false;
    ultralight::GetKeyIdentifierFromVirtualKeyCode(vk, event.key_identifier);
    p.view->FireKeyEvent(event);
}
void Renderer::blur() { impl_->runtime.checkThread(); impl_->view->Unfocus(); }
void Renderer::scale(double value) {
    if (!std::isfinite(value) || value < .1 || value > 4) throw std::runtime_error("Invalid card scale");
    auto& p = *impl_; p.runtime.checkThread(); p.scale = value;
    if (p.hostReady) p.eval("CallayersHost.scale(" + std::to_string(value) + ")");
}
unsigned Renderer::requestSnapshot() {
    auto& p = *impl_;
    if (!p.cardReady) throw std::runtime_error("Wait for the card to finish loading");
    return static_cast<unsigned>(std::stoul(p.eval("CallayersHost.snapshot()")));
}
std::string Renderer::snapshot(unsigned request) {
    const auto result = impl_->eval("CallayersHost.result(" + std::to_string(request) + ")");
    if (result.rfind("error:", 0) == 0) throw std::runtime_error(result.substr(6));
    return result;
}
const char* Renderer::sdkVersion() {
#if CALLAYERS_ULTRALIGHT_MAJOR == 1
    return "1.4.0";
#else
    return "2.0.0-beta.2";
#endif
}
#ifdef CALLAYERS_RENDERER_CHECKS
std::string Renderer::evaluateForTesting(const std::string& script) { return impl_->eval(script); }
#endif
}
