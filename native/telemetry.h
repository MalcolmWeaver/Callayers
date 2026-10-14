#pragma once
#include "telemetry_state.h"
#include <windows.h>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace callayers {
struct TelemetryCredentials {std::wstring url,uid,token;};
class TelemetrySender {
public:
    using Send=std::function<int(const TelemetryCredentials&,const std::string&)>;
private:
    struct Pending {std::string json;TelemetryCredentials credentials;ULONGLONG queued;};
    std::mutex mutex_;std::condition_variable wake_;std::deque<Pending> pending_;
    TelemetryCredentials credentials_;std::string session_;size_t inFlight_=0;
    bool stopping_=false;std::thread worker_;
    void work(Send send,std::filesystem::path diagnostic);
public:
    explicit TelemetrySender(Send send={},std::filesystem::path diagnostic={});
    ~TelemetrySender();
    void account(TelemetryCredentials value,bool newSession)noexcept;
    void record(TelemetryEvent event)noexcept;
};
}
