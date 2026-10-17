#pragma once
#include <Ultralight/Ultralight.h>
#include <Ultralight/platform/Clipboard.h>

#ifndef CALLAYERS_ULTRALIGHT_MAJOR
#error Select CALLAYERS_ULTRALIGHT_MAJOR=1 or 2 for the linked SDK
#endif
#if CALLAYERS_ULTRALIGHT_MAJOR != 1 && CALLAYERS_ULTRALIGHT_MAJOR != 2
#error Unsupported Ultralight API version
#endif

namespace callayers::mac {
// Card documents do not receive the system clipboard. Native editing can
// explicitly forward user-authorized text without granting background reads.
class PrivateClipboard final : public ultralight::Clipboard {
public:
    void Clear() override {}
#if CALLAYERS_ULTRALIGHT_MAJOR == 1
    ultralight::String ReadPlainText() override { return ""; }
    void WritePlainText(const ultralight::String&) override {}
#else
    ultralight::RefPtr<ultralight::ClipboardData> Read() override { return nullptr; }
    void Write(ultralight::RefPtr<ultralight::ClipboardData>) override {}
#endif
};
inline void Configure(ultralight::Config& config, ultralight::ViewConfig& view) {
    config.resource_path_prefix = "resources/";
    view.is_accelerated = false;
    view.is_transparent = true;
    view.initial_device_scale = 1.5; // 1280x720 logical scene -> 1920x1080 pixels.
#if CALLAYERS_ULTRALIGHT_MAJOR == 2
    config.memory_profile = ultralight::MemoryProfile::LowMemory;
    view.allow_universal_access_from_file_urls = false;
    view.clipboard_read_policy = ultralight::ClipboardReadPolicy::Deny;
#endif
}
}
