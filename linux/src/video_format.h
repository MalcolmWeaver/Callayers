#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

inline constexpr int CanvasWidth = 1280;
inline constexpr int CanvasHeight = 720;
inline constexpr int OutputWidth = 1920;
inline constexpr int OutputHeight = 1080;
inline constexpr int OutputFps = 30;
inline constexpr char OutputDevice[] = "/dev/video42";
inline constexpr int OutputNumber = 42;

struct FittedBox { int x = 0, y = 0, w = 1, h = 1; };

inline FittedBox FitInside(int sourceWidth, int sourceHeight, int destWidth, int destHeight) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || destWidth <= 0 || destHeight <= 0) return {0, 0, std::max(destWidth, 0), std::max(destHeight, 0)};
    double scale = std::min(double(destWidth) / sourceWidth, double(destHeight) / sourceHeight);
    int width = std::max(1, int(sourceWidth * scale));
    int height = std::max(1, int(sourceHeight * scale));
    return {(destWidth - width) / 2, (destHeight - height) / 2, width, height};
}

inline void BgraToYuyv(const uint8_t* bgra, int width, int height, std::vector<uint8_t>& yuyv) {
    yuyv.resize(size_t(width) * height * 2);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; x += 2) {
            auto sample = [&](int px, int& yy, int& uu, int& vv) {
                auto p = bgra + (size_t(y) * width + px) * 4;
                int b = p[0], g = p[1], r = p[2];
                yy = std::clamp((77 * r + 150 * g + 29 * b) >> 8, 0, 255);
                uu = std::clamp(((-43 * r - 85 * g + 128 * b) >> 8) + 128, 0, 255);
                vv = std::clamp(((128 * r - 107 * g - 21 * b) >> 8) + 128, 0, 255);
            };
            int y0, u0, v0, y1, u1, v1;
            sample(x, y0, u0, v0);
            sample(std::min(x + 1, width - 1), y1, u1, v1);
            auto out = yuyv.data() + (size_t(y) * width + x) * 2;
            out[0] = uint8_t(y0);
            out[1] = uint8_t((u0 + u1) / 2);
            out[2] = uint8_t(y1);
            out[3] = uint8_t((v0 + v1) / 2);
        }
    }
}
