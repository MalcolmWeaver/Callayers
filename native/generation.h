#pragma once
#include <windows.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include "generated_card.h"
namespace callayers {
struct GenerationResult {GeneratedCard card;std::wstring target;unsigned long long contentVersion=0;};
struct GenerationSnapshot {bool busy=false,applying=false;int remaining=-1,accessError=0;std::wstring message;std::optional<GenerationResult> result;};
class Generation {
    std::mutex mutex_;std::thread worker_;std::atomic<unsigned> version_{0};std::atomic<bool> working_{false};GenerationSnapshot state_;
    std::atomic<void*> activeRequest_{nullptr};
public:
    ~Generation();
    void start(const std::wstring& prompt,const std::wstring& token,std::optional<GeneratedCard> current=std::nullopt);
    void cancel();
    GenerationSnapshot snapshot();
    std::optional<GenerationResult> takeResult();
    int takeAccessError();
    void message(const std::wstring& value);
};
}
