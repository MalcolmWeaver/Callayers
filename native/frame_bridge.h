#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace callayers {
inline constexpr int Width = 1280, Height = 720, Fps = 30;
inline constexpr size_t PixelBytes = size_t(Width) * Height * 4;
inline constexpr ULONGLONG FreshMs = 1000;
inline constexpr wchar_t BridgeName[] = L"Local\\Callayers.Foundation.v2.Frames";
inline constexpr wchar_t LockName[] = L"Local\\Callayers.Foundation.v2.FrameLock";

// Fixed-size, same-session transport. Only complete pictures cross this boundary.
// The mutex is always tried without waiting on the video paths. An abandoned
// writer's frame is rejected; atomic invalidation does not require this mutex.
struct alignas(8) SharedFrame {
    volatile LONG active;
    LONG reserved;
    volatile LONG64 session;
    volatile LONG64 consumerTick;
    LONG64 frameTick;
    LONG64 frameSession;
    uint8_t pixels[PixelBytes];
};
inline LONG64 Read64(volatile LONG64* p) { return InterlockedCompareExchange64(p, 0, 0); }
inline LONG Read32(volatile LONG* p) { return InterlockedCompareExchange(p, 0, 0); }
inline void Neutral(std::vector<uint8_t>& pixels) {
    pixels.resize(PixelBytes);
    for (int y = 0; y < Height; ++y) for (int x = 0; x < Width; ++x) {
        auto p = &pixels[(size_t(y) * Width + x) * 4];
        bool bar = (y >= 348 && y < 372 && x >= 540 && x < 740);
        p[0] = bar ? 142 : 34; p[1] = bar ? 130 : 29; p[2] = bar ? 120 : 25; p[3] = 255;
    }
}
class FrameBridge {
    HANDLE mapping_ = nullptr, mutex_ = nullptr;
    SharedFrame* frame_ = nullptr;
    LONG64 copiedSession_ = 0, copiedTick_ = 0;
public:
    FrameBridge() {
        mutex_ = CreateMutexW(nullptr, FALSE, LockName);
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedFrame), BridgeName);
        if (mapping_) frame_ = static_cast<SharedFrame*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedFrame)));
    }
    ~FrameBridge() {
        if (frame_) UnmapViewOfFile(frame_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
    }
    FrameBridge(const FrameBridge&) = delete;
    FrameBridge& operator=(const FrameBridge&) = delete;
    bool valid() const { return frame_ && mutex_; }
    LONG64 begin() {
        stop();
        return valid() ? Read64(&frame_->session) : 0;
    }
    void stop() {
        if (frame_) { InterlockedExchange(&frame_->active, 0); InterlockedIncrement64(&frame_->session); }
    }
    bool consumerActive() const {
        if (!frame_) return false;
        auto tick = Read64(&frame_->consumerTick);
        return tick > 0 && GetTickCount64() - tick < FreshMs;
    }
    void heartbeat() { if (frame_) InterlockedExchange64(&frame_->consumerTick, GetTickCount64()); }
    void consumerStopped() { if (frame_) InterlockedExchange64(&frame_->consumerTick, 0); }
    void publish(const std::vector<uint8_t>& pixels, LONG64 session, ULONGLONG cameraTick) {
        if (!valid() || pixels.size() != PixelBytes || Read64(&frame_->session) != session) return;
        DWORD lock = WaitForSingleObject(mutex_, 0);
        if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED) return;
        memcpy(frame_->pixels, pixels.data(), PixelBytes);
        frame_->frameTick = cameraTick;
        frame_->frameSession = session;
        InterlockedExchange(&frame_->active, 1);
        ReleaseMutex(mutex_);
    }
    bool read(std::vector<uint8_t>& pixels) {
        if (!valid()) return false;
        LONG64 session = Read64(&frame_->session);
        if (!Read32(&frame_->active)) return false;
        DWORD lock = WaitForSingleObject(mutex_, 0);
        if (lock == WAIT_ABANDONED) {
            stop(); ReleaseMutex(mutex_); return false;
        }
        if (lock == WAIT_OBJECT_0) {
            pixels.resize(PixelBytes);
            memcpy(pixels.data(), frame_->pixels, PixelBytes);
            copiedTick_ = frame_->frameTick; copiedSession_ = frame_->frameSession;
            ReleaseMutex(mutex_);
        }
        return Read32(&frame_->active) && copiedSession_ == session &&
            Read64(&frame_->session) == session && copiedTick_ > 0 &&
            GetTickCount64() - copiedTick_ < FreshMs && pixels.size() == PixelBytes;
    }
};
}
