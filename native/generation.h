#pragma once
#include <windows.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include "generated_card.h"
namespace callayers {
struct GenerationSnapshot {bool busy=false;int remaining=-1,accessError=0;std::wstring message;std::optional<GeneratedCard> result;};
class Generation {
    std::mutex mutex_;std::thread worker_;std::atomic<unsigned> version_{0};std::atomic<bool> working_{false};GenerationSnapshot state_;
    std::atomic<void*> activeRequest_{nullptr};
public:
    ~Generation();
    void start(const std::wstring& prompt,const std::wstring& token);
    void cancel();
    GenerationSnapshot snapshot();
    std::optional<GeneratedCard> takeResult();
    int takeAccessError();
};
}
