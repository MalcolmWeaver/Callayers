#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace callayers {
class RendererLog;
struct OverlayImage { std::vector<uint8_t> pixels; ULONGLONG tick = 0; };
struct OverlayState {
    std::mutex mutex;
    std::vector<uint8_t> png;
    ULONGLONG tick = 0;
    unsigned epoch = 0;
    std::atomic<unsigned> currentEpoch{0};
    std::atomic<bool> ready{false}, failed{false};
};
class Overlay {
    HWND host_ = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> view_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::shared_ptr<RendererLog> log_;
    bool enabled_ = true, pending_ = false;
    bool initialDocumentPending_ = false, documentNavigationSeen_ = false;
    UINT64 documentNavigation_ = 0;
    std::wstring scene_;
    void configure(ICoreWebView2Environment* environment);
    void fail(const char* stage, HRESULT result, int detail = 0);
    void close();
public:
    std::shared_ptr<OverlayState> state = std::make_shared<OverlayState>();
    ~Overlay();
    void initialize(HINSTANCE instance);
    void retry(HINSTANCE instance);
    void update(const std::wstring& json);
    void enable(bool enabled);
    void capture();
};
std::shared_ptr<OverlayImage> DecodeOverlay(const std::vector<uint8_t>& png, ULONGLONG tick);
}
