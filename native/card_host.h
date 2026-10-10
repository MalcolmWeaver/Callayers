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
    CardHost();
    ~CardHost();
    void SetScene(const Scene& scene);
    std::shared_ptr<const TextOverlay> Latest() const;
    HRESULT Error() const;
    uint64_t FrameCount() const;
};
}
