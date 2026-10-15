#include "auth_callback.h"
#include "video_format.h"
#include <cstdlib>
#include <iostream>

int main() {
    auto full = FitInside(1920, 1080, 1920, 1080);
    if (full.x != 0 || full.y != 0 || full.w != 1920 || full.h != 1080) return 1;
    auto bars = FitInside(640, 480, 1920, 1080);
    if (bars.w >= 1920 || bars.h != 1080 || bars.x <= 0 || bars.y != 0) return 1;
    std::vector<uint8_t> bgra(8, 0);
    bgra[2] = 255;
    bgra[6] = 255;
    std::vector<uint8_t> yuyv;
    BgraToYuyv(bgra.data(), 2, 1, yuyv);
    if (yuyv.size() != 4 || yuyv[0] < 70) return 1;
    auto callback = ParseAuthCallback("/callback?state=abc&code=xyz", "abc");
    if (callback.kind != CallbackKind::Code || callback.code != "xyz") return 1;
    if (ParseAuthCallback("/callback?state=nope&code=xyz", "abc").kind != CallbackKind::Ignore) return 1;
    if (ParseAuthCallback("/callback?state=abc&error=access_denied", "abc").kind != CallbackKind::Cancelled) return 1;
    if (AuthUrlEncode("a b") != "a%20b") return 1;
    std::cout << "linux checks passed\n";
    return 0;
}
