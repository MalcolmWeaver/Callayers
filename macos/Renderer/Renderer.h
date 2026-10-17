#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace callayers::mac {
// The renderer and every call into it belong to the creating (UI) thread.
// Pixel memory belongs to Frame and remains valid until the next tick.
struct Frame {
    unsigned width = 0, height = 0, rowBytes = 0;
    std::vector<uint8_t> bgra;
    uint64_t serial = 0;
};

class Renderer {
public:
    explicit Renderer(const std::string& resourceRoot);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void loadCard(std::string html, std::string state = "{}", unsigned width = 640, unsigned height = 400);
    void clear();
    bool tick();
    bool ready() const;
    const Frame& frame() const;
    const std::string& problem() const;
    void mouse(int type, int x, int y, int button);
    void scroll(int x, int y, int deltaX, int deltaY);
    void key(int type, int virtualKey, int nativeKey, unsigned modifiers,
             const std::string& text, const std::string& unmodifiedText, bool repeat);
    void blur();
    void scale(double value);
    unsigned requestSnapshot();
    // Empty means pending; errors throw. A completed result is bounded JSON.
    std::string snapshot(unsigned request);
    static const char* sdkVersion();
#ifdef CALLAYERS_RENDERER_CHECKS
    std::string evaluateForTesting(const std::string& script);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
