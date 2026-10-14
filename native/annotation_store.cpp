#include "annotation_store.h"
#include <shlobj.h>
#include <winrt/base.h>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace callayers {
static HRESULT WriteWorkspace(const std::filesystem::path& path,const std::string& json) {
    if(path.empty())return E_FAIL;
    std::error_code error;std::filesystem::create_directories(path.parent_path(),error);if(error)return HRESULT_FROM_WIN32(error.value());
    auto temporary=path;temporary+=L".pending";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return HRESULT_FROM_WIN32(GetLastError());
    DWORD written=0;BOOL ok=WriteFile(file,json.data(),DWORD(json.size()),&written,nullptr);DWORD failure=ok?ERROR_WRITE_FAULT:GetLastError();
    if(ok && written==json.size()){ok=FlushFileBuffers(file);failure=GetLastError();}else ok=FALSE;
    CloseHandle(file);
    if(!ok){DeleteFileW(temporary.c_str());return HRESULT_FROM_WIN32(failure);}
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){failure=GetLastError();DeleteFileW(temporary.c_str());return HRESULT_FROM_WIN32(failure);}
    return S_OK;
}
AnnotationStore::AnnotationStore(const wchar_t* fileName,const wchar_t* property,const wchar_t* fallback,size_t maxBytes):property_(property),maxBytes_(maxBytes) {
    PWSTR local=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))path_=std::filesystem::path(local)/L"Callayers"/L"WorkspaceV2"/fileName;
    CoTaskMemFree(local);
    if(fallback && !path_.empty())fallback_=path_.parent_path()/fallback;
    writer_=std::thread([this]{
        for(;;){std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stopping_||pending_.has_value();});if(!pending_)break;auto job=std::move(*pending_);pending_.reset();lock.unlock();HRESULT error=E_FAIL;try{error=WriteWorkspace(path_,job.json);}catch(...){}lock.lock();result_={job.revision,error};}
    });
}
AnnotationStore::~AnnotationStore(){{std::lock_guard lock(mutex_);stopping_=true;}wake_.notify_one();writer_.join();}
std::wstring AnnotationStore::load(HRESULT& error) {
    error=S_OK;
    if(path_.empty()){error=E_FAIL;return L"";}
    auto source=path_;std::error_code ioError;bool exists=std::filesystem::exists(source,ioError);
    if(!exists && !ioError && !fallback_.empty()){source=fallback_;exists=std::filesystem::exists(source,ioError);}
    if(ioError){error=HRESULT_FROM_WIN32(ioError.value());return L"";}if(!exists)return L"[]";
    auto size=std::filesystem::file_size(source,ioError);if(ioError || size>maxBytes_){error=E_FAIL;return L"";}
    std::ifstream file(source,std::ios::binary);if(!file){error=HRESULT_FROM_WIN32(ERROR_READ_FAULT);return L"";}
    std::string json{std::istreambuf_iterator<char>(file),{}};
    if(file.bad()){error=HRESULT_FROM_WIN32(ERROR_READ_FAULT);return L"";}
    try {
        auto root=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(json));
        if(root.GetNamedNumber(L"version")!=2)throw std::runtime_error("Unknown workspace format");
        return std::wstring(root.GetNamedArray(property_).Stringify());
    }catch(...){error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);return L"";}
}
unsigned AnnotationStore::save(const std::wstring& annotations) {
    auto json=std::string("{\"version\":2,\"")+winrt::to_string(property_)+"\":"+winrt::to_string(annotations)+"}";
    std::lock_guard lock(mutex_);unsigned revision=++revision_;
    pending_=Pending{revision,std::move(json)};wake_.notify_one();return revision;
}
AnnotationSaveResult AnnotationStore::result(){std::lock_guard lock(mutex_);return result_;}
}
