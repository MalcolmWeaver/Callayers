#pragma once
#include <Ultralight/platform/FileSystem.h>
#include <AppCore/Platform.h>
#include <filesystem>
#include <winrt/base.h>
namespace callayers {
// Only packaged renderer resources can be opened by WebCore. User HTML, state,
// credentials and the native application's files are never in this file system.
class OfflineFiles final : public ultralight::FileSystem {
    std::filesystem::path base_,resources_;
    ultralight::FileSystem* delegate_;
    bool allowed(const ultralight::String& name)const{
        try{auto bytes=name.utf8();auto path=std::filesystem::path(std::wstring(winrt::to_hstring(std::string(bytes.data(),bytes.length()))));
            if(!path.is_absolute())path=base_/path;path=path.lexically_normal();
            auto relative=path.lexically_relative(resources_);if(relative.empty()||relative.is_absolute())return false;
            for(auto& part:relative)if(part==L"..")return false;
            return relative==L"icudt67l.dat"||relative==L"cacert.pem";
        }catch(...){return false;}
    }
public:
    explicit OfflineFiles(const std::filesystem::path& base):base_(base),resources_(base/L"resources"),delegate_(ultralight::GetPlatformFileSystem(base.string().c_str())){}
    bool FileExists(const ultralight::String& path)override{return allowed(path)&&delegate_->FileExists(path);}
    ultralight::String GetFileMimeType(const ultralight::String& path)override{return allowed(path)?delegate_->GetFileMimeType(path):"application/unknown";}
    ultralight::String GetFileCharset(const ultralight::String& path)override{return allowed(path)?delegate_->GetFileCharset(path):"utf-8";}
    ultralight::RefPtr<ultralight::Buffer> OpenFile(const ultralight::String& path)override{return allowed(path)?delegate_->OpenFile(path):nullptr;}
};
}
