#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <functional>
#include <memory>
#include <string>

namespace callayers {
class TextEditor {
    HWND parent_=nullptr,host_=nullptr;
    RECT area_{};
    double scale_=1,height_=1;
    int inset_=6;
    bool ready_=false,unavailable_=false,backdropReady_=false,shown_=false;
    std::wstring text_,options_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> view_;
    std::shared_ptr<bool> alive_=std::make_shared<bool>(false);
    std::function<void(bool)> finish_;
    void resize();
    void failed();
    void configure(ICoreWebView2Environment* environment);
    static LRESULT CALLBACK HostProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam);
public:
    ~TextEditor();
    void open(HWND parent,const RECT& area,const std::wstring& text,double font,const std::wstring& color,double width,double scale,double minimumHeight,bool waitForBackdrop,std::function<void(bool)> finish);
    void close();
    bool active() const {return host_!=nullptr;}
    const std::wstring& text() const {return text_;}
    double height() const {return height_;}
    bool unavailable() const {return unavailable_;}
    void focus();
    void backdrop(bool clean);
};
}
