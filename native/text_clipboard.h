#pragma once
#include <windows.h>
#include <Ultralight/Ultralight.h>
#include <Ultralight/platform/Clipboard.h>
#include <winrt/base.h>
#include <cstring>

namespace callayers {
// Only synchronous trusted-editor input can access the OS clipboard.
// Generated views are denied both reads and writes.
class TextClipboard final : public ultralight::Clipboard {
public:
    HWND owner=nullptr;
    void Clear() override {
        if(owner && OpenClipboard(owner)){EmptyClipboard();CloseClipboard();}
    }
    ultralight::RefPtr<ultralight::ClipboardData> Read() override {
        auto data=ultralight::ClipboardData::Create();
        if(!owner || !OpenClipboard(owner))return data;
        auto handle=GetClipboardData(CF_UNICODETEXT);
        if(handle){auto value=static_cast<const wchar_t*>(GlobalLock(handle));
            if(value){auto count=GlobalSize(handle)/sizeof(wchar_t);size_t length=0;while(length<count && value[length])++length;
                if(length<count && length<=1024*1024)data->Set("text/plain",winrt::to_string(std::wstring_view(value,length)).c_str());GlobalUnlock(handle);}}
        CloseClipboard();return data;
    }
    void Write(ultralight::RefPtr<ultralight::ClipboardData> data) override {
        if(!owner || !data || !data->Has("text/plain"))return;
        auto bytes=data->AsText().utf8();auto text=winrt::to_hstring(std::string_view(bytes.data(),bytes.length()));
        auto handle=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));if(!handle)return;
        auto target=GlobalLock(handle);if(!target){GlobalFree(handle);return;}
        memcpy(target,text.c_str(),(text.size()+1)*sizeof(wchar_t));GlobalUnlock(handle);
        if(!OpenClipboard(owner)){GlobalFree(handle);return;}
        if(!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT,handle))GlobalFree(handle);
        CloseClipboard();
    }
};
inline TextClipboard& PrivateTextClipboard(){static TextClipboard clipboard;return clipboard;}
class TextClipboardInput {
    HWND previous_;
public:
    explicit TextClipboardInput(HWND window):previous_(PrivateTextClipboard().owner){PrivateTextClipboard().owner=window;}
    ~TextClipboardInput(){PrivateTextClipboard().owner=previous_;}
};
}
