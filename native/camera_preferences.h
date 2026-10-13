#pragma once
#include <windows.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace callayers {
struct CameraPreference {
    std::wstring identity, name;
};
struct PreferenceResult {
    unsigned revision = 0;
    HRESULT error = S_OK;
    CameraPreference saved; // Last successful write, even when a newer save fails.
};
// A single REG_MULTI_SZ value holds both fields. Each write replaces the pair
// together; a failed write leaves the prior working choice available.
class CameraPreferences {
    struct Pending { unsigned revision; CameraPreference camera; };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Pending> pending_;
    PreferenceResult result_;
    unsigned revision_ = 0;
    bool stopping_ = false;
    std::thread writer_;
public:
    CameraPreferences();
    ~CameraPreferences();
    CameraPreference load(HRESULT& error);
    unsigned save(CameraPreference camera);
    PreferenceResult result();
};
}
