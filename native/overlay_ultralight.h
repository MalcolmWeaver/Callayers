#pragma once
#include <windows.h>
#include <Ultralight/Ultralight.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace callayers {
class RendererLog;
struct OverlayImage {std::vector<uint8_t> pixels;ULONGLONG tick=0;};
struct OverlayState {
    std::mutex mutex;
    std::shared_ptr<OverlayImage> image,privateImage;
    uint64_t privateTextId=0;
    unsigned epoch=0;
    std::atomic<unsigned> currentEpoch{0};
    std::atomic<bool> ready{false},failed{false};
    std::atomic<uint64_t> requestedPrivateTextId{0};
};
// Renderer/view access is confined to the UI thread. Published pool entries are
// immutable while held by the compositor; exhausted pools drop frames.
class Overlay {
    ultralight::RefPtr<ultralight::Renderer> renderer_;
    ultralight::RefPtr<ultralight::View> view_;
    std::array<std::shared_ptr<OverlayImage>,3> frames_,privateFrames_;
    std::shared_ptr<RendererLog> log_;
    std::wstring scene_;
    bool enabled_=true,scenePending_=true;
    ULONGLONG opened_=0;
    void script(const std::string& code);
    std::shared_ptr<OverlayImage> copy(std::array<std::shared_ptr<OverlayImage>,3>& pool,ULONGLONG tick);
    void fail(HRESULT error);
public:
    std::shared_ptr<OverlayState> state=std::make_shared<OverlayState>();
    ~Overlay();
    void initialize(HINSTANCE instance);
    void retry(HINSTANCE instance);
    void update(const std::wstring& json);
    void enable(bool enabled);
    void capture();
    void privatePreview(uint64_t textId){state->requestedPrivateTextId=textId;}
};
}
