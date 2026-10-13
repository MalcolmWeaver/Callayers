#pragma once
#include <windows.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace callayers {
// Aggregate timings only: no frame contents or device identifiers. Timings are
// elapsed wall time, not CPU attribution; async capture includes renderer/wait.
enum class VideoStage { CameraCopy, OverlayCapture, OverlayDecode, Compose, Publish, Count };
class ResourceDiagnostics {
    struct Sample { unsigned long long count = 0; double total = 0, peak = 0; };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::array<Sample, size_t(VideoStage::Count)> samples_{};
    std::vector<std::string> events_;
    bool done_ = false;
    std::thread writer_;
public:
    ResourceDiagnostics() {
        wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
        auto directory = std::filesystem::path(exe).parent_path() / L"diagnostics";
        writer_ = std::thread([this, directory] {
            std::error_code error; std::filesystem::create_directories(directory, error);
            std::ofstream file(directory / L"resources.log", std::ios::trunc);
            file << "Callayers resources; build " << __DATE__ << ' ' << __TIME__
                 << "\nTimings: elapsed wall milliseconds, not CPU. overlay_capture includes async wait. camera_copy excludes Media Foundation conversion.\n";
            constexpr const char* names[] = {"camera_copy", "overlay_capture", "overlay_decode", "compose", "publish"};
            auto previous = std::chrono::steady_clock::now();
            for (;;) {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock, std::chrono::seconds(5), [this] { return done_; });
                auto now = std::chrono::steady_clock::now();
                double seconds = std::chrono::duration<double>(now - previous).count(); previous = now;
                auto samples = samples_; samples_ = {};
                auto events = std::move(events_); events_.clear(); bool done = done_; lock.unlock();
                file << std::fixed << std::setprecision(3) << "tick=" << GetTickCount64() << " interval_seconds=" << seconds << '\n';
                for (const auto& event : events) file << event << '\n';
                for (size_t i = 0; i < samples.size(); ++i) {
                    const auto& s = samples[i];
                    file << names[i] << " count=" << s.count << " rate_per_second=" << (seconds > 0 ? s.count / seconds : 0)
                         << " total_ms=" << s.total << " mean_ms=" << (s.count ? s.total / s.count : 0) << " peak_ms=" << s.peak << '\n';
                }
                file.flush(); if (done) break;
            }
        });
    }
    ~ResourceDiagnostics() {
        { std::lock_guard lock(mutex_); done_ = true; } wake_.notify_one(); writer_.join();
    }
    void event(std::string text) {
        std::lock_guard lock(mutex_); if (events_.size() < 64) events_.push_back(std::move(text));
    }
    void sample(VideoStage stage, double milliseconds) {
        std::lock_guard lock(mutex_); auto& s = samples_[size_t(stage)];
        ++s.count; s.total += milliseconds; if (milliseconds > s.peak) s.peak = milliseconds;
    }
};
inline ResourceDiagnostics& Resources() { static ResourceDiagnostics diagnostics; return diagnostics; }
using DiagnosticClock = std::chrono::steady_clock;
inline void RecordDuration(VideoStage stage, DiagnosticClock::time_point start) {
    Resources().sample(stage, std::chrono::duration<double, std::milli>(DiagnosticClock::now() - start).count());
}
class StageTimer {
    VideoStage stage_; DiagnosticClock::time_point start_ = DiagnosticClock::now();
public:
    explicit StageTimer(VideoStage stage) : stage_(stage) {}
    ~StageTimer() { RecordDuration(stage_, start_); }
};
}
