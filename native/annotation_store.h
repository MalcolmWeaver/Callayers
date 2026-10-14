#pragma once
#include <windows.h>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace callayers {
struct AnnotationSaveResult {unsigned revision=0;HRESULT error=S_OK;};
class AnnotationStore {
    struct Pending {unsigned revision;std::string json;};
    std::filesystem::path path_;
    std::filesystem::path fallback_;
    size_t maxBytes_=16*1024*1024;
    std::wstring property_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Pending> pending_;
    AnnotationSaveResult result_;
    unsigned revision_=0;
    bool stopping_=false;
    std::thread writer_;
public:
    AnnotationStore(const wchar_t* fileName=L"workspace.json",const wchar_t* property=L"annotations",const wchar_t* fallback=nullptr,size_t maxBytes=16*1024*1024);
    ~AnnotationStore();
    std::wstring load(HRESULT& error);
    unsigned save(const std::wstring& annotations);
    AnnotationSaveResult result();
};
}
