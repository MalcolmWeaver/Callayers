#pragma once

// Shared frame published by the app and read by the virtual camera media
// source. Frame Server runs in session 0, and a standard user cannot create
// a Global\ section, so both sides map the same file under ProgramData.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>

#include <cstdint>
#include <cstring>

namespace callayers {

inline constexpr wchar_t kFramePath[] = L"C:\\ProgramData\\Callayers\\frames.bin";
inline constexpr wchar_t kCameraName[] = L"Callayers";
// {7C4E2A11-6B8F-4D3E-A1C5-9F0E2B7D4A18}
inline constexpr GUID kActivateClsid = {
    0x7c4e2a11, 0x6b8f, 0x4d3e, {0xa1, 0xc5, 0x9f, 0x0e, 0x2b, 0x7d, 0x4a, 0x18}};
inline constexpr wchar_t kActivateClsidString[] = L"{7C4E2A11-6B8F-4D3E-A1C5-9F0E2B7D4A18}";
inline constexpr uint32_t kMagic = 0x524C5943; // 'CYLR'
inline constexpr uint32_t kVersion = 1;
inline constexpr uint32_t kWidth = 1920;
inline constexpr uint32_t kHeight = 1080;
inline constexpr uint32_t kFps = 30;
inline constexpr uint32_t kSlotCount = 3;
inline constexpr uint32_t kHeaderBytes = 4096;
inline constexpr uint32_t kNv12Size = kWidth * kHeight * 3 / 2;
inline constexpr uint32_t kMappingBytes = kHeaderBytes + kSlotCount * kNv12Size;
inline constexpr uint32_t kFlagProducerClosed = 1;
inline constexpr double kHeartbeatStaleMs = 500.0;

// Zero-initialized shared memory. Publishes use Interlocked* so the values
// stay aligned 32/64-bit integers with full barriers.
struct FrameHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t slotCount;
    volatile LONG sequence;
    volatile LONG latestSlot;
    volatile LONG flags;
    volatile LONG64 heartbeatQpc;
};

inline uint8_t* SlotBytes(void* view, uint32_t slot) {
    return static_cast<uint8_t*>(view) + kHeaderBytes + slot * kNv12Size;
}

inline SECURITY_ATTRIBUTES* FrameSecurity(SECURITY_ATTRIBUTES* sa, PSECURITY_DESCRIPTOR* sd) {
    *sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;IU)(A;;GA;;;BU)",
            SDDL_REVISION_1,
            sd,
            nullptr)) {
        return nullptr;
    }
    sa->nLength = sizeof(*sa);
    sa->lpSecurityDescriptor = *sd;
    sa->bInheritHandle = FALSE;
    return sa;
}

struct MappedFrames {
    HANDLE file = nullptr;
    HANDLE mapping = nullptr;
    void* view = nullptr;

    FrameHeader* Header() const { return static_cast<FrameHeader*>(view); }

    bool OpenOrCreate() {
        if (view) {
            return true;
        }
        CreateDirectoryW(L"C:\\ProgramData\\Callayers", nullptr);
        SECURITY_ATTRIBUTES sa{};
        PSECURITY_DESCRIPTOR sd = nullptr;
        file = CreateFileW(
            kFramePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FrameSecurity(&sa, &sd),
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (sd) {
            LocalFree(sd);
        }
        if (file == INVALID_HANDLE_VALUE) {
            file = nullptr;
            return false;
        }
        mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, kMappingBytes, nullptr);
        if (!mapping) {
            CloseHandle(file);
            file = nullptr;
            return false;
        }
        view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, kMappingBytes);
        if (!view) {
            CloseHandle(mapping);
            CloseHandle(file);
            mapping = nullptr;
            file = nullptr;
            return false;
        }
        auto* header = Header();
        if (header->magic != kMagic) {
            header->version = kVersion;
            header->width = kWidth;
            header->height = kHeight;
            header->slotCount = kSlotCount;
            header->sequence = 0;
            header->latestSlot = 0;
            header->flags = 0;
            header->heartbeatQpc = 0;
            MemoryBarrier();
            header->magic = kMagic;
        }
        return true;
    }

    void Close() {
        if (view) {
            UnmapViewOfFile(view);
            view = nullptr;
        }
        if (mapping) {
            CloseHandle(mapping);
            mapping = nullptr;
        }
        if (file) {
            CloseHandle(file);
            file = nullptr;
        }
    }

    void Publish(const uint8_t* nv12) {
        if (!view) {
            return;
        }
        auto* header = Header();
        const LONG slot = (header->latestSlot + 1) % static_cast<LONG>(kSlotCount);
        memcpy(SlotBytes(view, static_cast<uint32_t>(slot)), nv12, kNv12Size);
        MemoryBarrier();
        InterlockedExchange(&header->latestSlot, slot);
        InterlockedIncrement(&header->sequence);
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        InterlockedExchange64(&header->heartbeatQpc, now.QuadPart);
        InterlockedAnd(&header->flags, ~static_cast<LONG>(kFlagProducerClosed));
    }

    void MarkClosed() {
        if (!view) {
            return;
        }
        InterlockedOr(&Header()->flags, static_cast<LONG>(kFlagProducerClosed));
    }

    // Copies the newest finished frame. Returns false when the producer is
    // closed, stale, or the section is not a Callayers frame.
    bool TryCopyLatest(uint8_t* dest) const {
        if (!view) {
            return false;
        }
        auto* header = Header();
        if (header->magic != kMagic || header->version != kVersion) {
            return false;
        }
        if (header->flags & static_cast<LONG>(kFlagProducerClosed)) {
            return false;
        }
        const LONG64 stamp = InterlockedCompareExchange64(&header->heartbeatQpc, 0, 0);
        if (stamp == 0) {
            return false;
        }
        LARGE_INTEGER now{};
        LARGE_INTEGER freq{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        const double ageMs = (static_cast<double>(now.QuadPart - stamp) * 1000.0) / static_cast<double>(freq.QuadPart);
        if (ageMs > kHeartbeatStaleMs) {
            return false;
        }
        for (int attempt = 0; attempt < 4; ++attempt) {
            const LONG seq1 = InterlockedCompareExchange(&header->sequence, 0, 0);
            const LONG slot = InterlockedCompareExchange(&header->latestSlot, 0, 0);
            if (slot < 0 || slot >= static_cast<LONG>(kSlotCount)) {
                return false;
            }
            memcpy(dest, SlotBytes(view, static_cast<uint32_t>(slot)), kNv12Size);
            const LONG seq2 = InterlockedCompareExchange(&header->sequence, 0, 0);
            if (seq1 == seq2 && seq1 != 0) {
                return true;
            }
        }
        return false;
    }
};

inline void WriteIdleNv12(uint8_t* dest) {
    memset(dest, 16, kWidth * kHeight);
    const int rectW = 480;
    const int rectH = 270;
    const int originX = (kWidth - rectW) / 2;
    const int originY = (kHeight - rectH) / 2;
    for (int y = originY; y < originY + rectH; ++y) {
        memset(dest + y * kWidth + originX, 96, rectW);
    }
    memset(dest + kWidth * kHeight, 128, kWidth * kHeight / 2);
}

inline void ConvertBgraToNv12(const uint8_t* bgra, int bgraStride, uint8_t* nv12) {
    uint8_t* yPlane = nv12;
    uint8_t* uvPlane = nv12 + kWidth * kHeight;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        const uint8_t* row = bgra + y * bgraStride;
        uint8_t* yRow = yPlane + y * kWidth;
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            const int b = row[x * 4 + 0];
            const int g = row[x * 4 + 1];
            const int r = row[x * 4 + 2];
            int Y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            if (Y < 0) Y = 0;
            if (Y > 255) Y = 255;
            yRow[x] = static_cast<uint8_t>(Y);
        }
    }
    for (int y = 0; y < static_cast<int>(kHeight); y += 2) {
        const uint8_t* row = bgra + y * bgraStride;
        uint8_t* uvRow = uvPlane + (y / 2) * kWidth;
        for (int x = 0; x < static_cast<int>(kWidth); x += 2) {
            const int b = row[x * 4 + 0];
            const int g = row[x * 4 + 1];
            const int r = row[x * 4 + 2];
            int U = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            int V = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
            if (U < 0) U = 0;
            if (U > 255) U = 255;
            if (V < 0) V = 0;
            if (V > 255) V = 255;
            uvRow[x] = static_cast<uint8_t>(U);
            uvRow[x + 1] = static_cast<uint8_t>(V);
        }
    }
}

inline int BarXAt(LARGE_INTEGER qpc, LARGE_INTEGER freq) {
    const double seconds = static_cast<double>(qpc.QuadPart) / static_cast<double>(freq.QuadPart);
    const int travel = static_cast<int>(kWidth) - 64;
    const double wrapped = seconds * 280.0;
    return static_cast<int>(wrapped) % travel;
}

inline void FillTestPatternBgra(uint8_t* bgra, int stride, int barX) {
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        uint8_t* row = bgra + y * stride;
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            const bool bar = x >= barX && x < barX + 64;
            row[x * 4 + 0] = bar ? 40 : 28;
            row[x * 4 + 1] = bar ? 190 : 32;
            row[x * 4 + 2] = bar ? 245 : 22;
            row[x * 4 + 3] = 255;
        }
    }
}

} // namespace callayers
