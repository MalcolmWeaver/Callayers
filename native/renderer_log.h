#pragma once
#include <windows.h>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

namespace callayers {
// Startup/failure diagnostics only. No images, card contents, URLs or per-frame
// logging. File writes run on their own thread, outside the UI and video paths.
class RendererLog {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::string> lines_;
    bool done_ = false;
    std::thread writer_;
public:
    RendererLog() {
        wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
        auto directory = std::filesystem::path(exe).parent_path() / L"diagnostics";
        writer_ = std::thread([this, directory] {
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            std::ofstream file(directory / L"renderer.log", std::ios::trunc);
            file << "Callayers renderer; build " << __DATE__ << ' ' << __TIME__ << '\n';
            for (;;) {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return done_ || !lines_.empty(); });
                if (lines_.empty() && done_) break;
                auto line = std::move(lines_.front()); lines_.pop_front(); lock.unlock();
                file << line << '\n'; file.flush();
            }
        });
    }
    ~RendererLog() {
        { std::lock_guard lock(mutex_); done_ = true; }
        wake_.notify_one();
        writer_.join();
    }
    void record(const char* stage, HRESULT result = S_OK, int detail = 0) {
        std::ostringstream line;
        line << GetTickCount64() << ' ' << stage << " result=0x" << std::hex << std::setw(8) << std::setfill('0') << static_cast<unsigned long>(result) << std::dec << " detail=" << detail;
        { std::lock_guard lock(mutex_); if (lines_.size() < 128) lines_.push_back(line.str()); }
        wake_.notify_one();
    }
};
}
