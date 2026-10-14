#pragma once
#include <windows.h>
#include <Ultralight/Ultralight.h>
#include <functional>
#include <string>

namespace callayers {
// UI-thread-only, private CPU view sharing the card renderer, never its video surface.
class TextEditor : public ultralight::EditorListener {
    HWND parent_=nullptr,host_=nullptr;
    RECT area_{};
    double scale_=1,height_=1;
    int inset_=6;
    wchar_t pendingHighSurrogate_=0;
    bool ready_=false,shown_=false,backdropReady_=false,unavailable_=false,ending_=false;
    bool ticking_=false,discardingComposition_=false;
    ULONGLONG opened_=0;
    std::wstring text_,options_;
    ultralight::RefPtr<ultralight::Renderer> renderer_;
    ultralight::RefPtr<ultralight::View> view_;
    HDC bitmapDC_=nullptr;
    HBITMAP bitmap_=nullptr;
    HGDIOBJ oldBitmap_=nullptr;
    void* pixels_=nullptr;
    int bitmapWidth_=0,bitmapHeight_=0;
    std::function<void(bool)> finish_;
    void resize();
    void failed();
    void poll();
    void tick();
    void paint(HDC dc);
    void key(UINT message,WPARAM wParam,LPARAM lParam);
    void composition(LPARAM flags);
    void releaseBitmap();
    void OnUpdateComposition(ultralight::View*) override;
    void OnDiscardComposition(ultralight::View*) override;
    static LRESULT CALLBACK HostProc(HWND,UINT,WPARAM,LPARAM);
public:
    ~TextEditor();
    void open(ultralight::Renderer*,HWND parent,const RECT& area,const std::wstring& text,double font,const std::wstring& color,double width,double scale,double minimumHeight,bool waitForBackdrop,std::function<void(bool)> finish);
    void close();
    bool active() const {return host_!=nullptr;}
    std::wstring text();
    double height() const {return height_;}
    bool unavailable() const {return unavailable_;}
    bool owns(HWND window) const {return host_ && window==host_;}
    void focus();
    void backdrop(bool clean);
};
}
