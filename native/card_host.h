#pragma once
#include "text_renderer.h"
#include <functional>
#include <memory>

namespace callayers {
// Browser and capture objects live on their own STA, independently of editor visibility.
class CardHost {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit CardHost(bool thumbnails=false);
    // A 480x270 BGRA atlas, available only for the private sidebar renderer.
    std::shared_ptr<const std::vector<BYTE>> ThumbnailPixels() const;
    ~CardHost();
    void SetScene(const Scene& scene);
    std::shared_ptr<const TextOverlay> Latest() const;
    HRESULT Error() const;
    uint64_t FrameCount() const;
};
}
