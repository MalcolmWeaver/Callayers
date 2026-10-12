#pragma once
#include "text_renderer.h"
#include <functional>
#include <memory>

namespace callayers {
// Browser and capture objects live on their own STA, independently of editor visibility.
struct CardStateChange {std::wstring id,html,state;uint64_t epoch=0;};
class CardHost {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit CardHost(bool thumbnails=false);
    // A 480x270 BGRA atlas, available only for the private sidebar renderer.
    std::shared_ptr<const std::vector<BYTE>> ThumbnailPixels() const;
    ~CardHost();
    void SetActive(bool active);
    void SetScene(const Scene& scene,uint64_t epoch=0);
    void Mouse(UINT message,POINT point,WPARAM buttons);
    void Key(UINT message,WPARAM key,LPARAM flags);
    void InsertText(const std::wstring& text);
    std::vector<CardStateChange> TakeStates();
    bool FlushState();
    std::shared_ptr<const TextOverlay> Latest() const;
    HRESULT Error() const;
    uint64_t FrameCount() const;
};
}
