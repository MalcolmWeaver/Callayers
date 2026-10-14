#include <windows.h>
#include <winrt/base.h>
#include "text_style.h"
#include <algorithm>
#include <vector>
#ifdef TEST_ULTRALIGHT
#include <Ultralight/Ultralight.h>
#include <AppCore/Platform.h>
#else
#include "overlay.h"
#endif

namespace {
constexpr int W=1280,H=720;
const wchar_t* Scene=L"{\"cards\":[{\"id\":0,\"visible\":true,\"x\":60,\"y\":70,\"w\":440,\"h\":145,\"contentW\":440,\"contentH\":145},{\"id\":1,\"visible\":true,\"x\":650,\"y\":70,\"w\":440,\"h\":165,\"contentW\":440,\"contentH\":165},{\"id\":2,\"visible\":true,\"x\":60,\"y\":410,\"w\":340,\"h\":145,\"contentW\":340,\"contentH\":145}],\"annotations\":[]}";
struct Test {
    std::vector<unsigned char> pixels=std::vector<unsigned char>(W*H*4);
    std::vector<unsigned char> preview=std::vector<unsigned char>(W*H*4);
    bool cards=true, sceneSet=false, failed=false;
    unsigned long long frames=0;
#ifdef TEST_ULTRALIGHT
    ultralight::RefPtr<ultralight::Renderer> renderer;
    ultralight::RefPtr<ultralight::View> view;
#else
    callayers::Overlay overlay;
    ULONGLONG lastTick=0;
#endif
    void start(HINSTANCE instance) {
#ifdef TEST_ULTRALIGHT
        (void)instance;
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
        auto base=std::filesystem::path(exe).parent_path();
        auto& platform=ultralight::Platform::instance();
        ultralight::Config config;config.memory_profile=ultralight::MemoryProfile::LowMemory;
        config.resource_path_prefix="resources/";
        platform.set_config(config);
        platform.set_font_loader(ultralight::GetPlatformFontLoader());
        platform.set_file_system(ultralight::GetPlatformFileSystem(base.string().c_str()));
        platform.set_logger(ultralight::GetDefaultLogger((base/"ultralight.log").string().c_str()));
        renderer=ultralight::Renderer::Create();
        ultralight::ViewConfig options;options.is_accelerated=false;options.is_transparent=true;
        view=renderer->CreateView(W,H,options,nullptr);
        auto html=winrt::to_string(callayers::ReadTextDocument(L"overlay.html"));
        view->LoadHTML(html.c_str());
#else
        overlay.update(Scene);overlay.initialize(instance);
#endif
    }
    void toggle() {
        cards=!cards; sceneSet=false;
#ifndef TEST_ULTRALIGHT
        overlay.update(cards?Scene:L"{\"cards\":[],\"annotations\":[]}");
#endif
    }
    void tick(HWND window) {
#ifdef TEST_ULTRALIGHT
        renderer->Update();
        if(!view->is_loading() && !sceneSet){
            ultralight::String exception;
            auto script=cards?"window.setScene("+winrt::to_string(Scene)+")":"window.stopScene()";
            view->EvaluateScript(script.c_str(),&exception);
            if(!exception.empty())throw std::runtime_error("Card scene script failed; see ultralight.log");
            sceneSet=true;
        }
        renderer->Render();
        auto surface=view->surface();
        if(surface){
            auto source=static_cast<const unsigned char*>(surface->LockPixels());
            if(source){for(int y=0;y<H;++y)memcpy(pixels.data()+size_t(y)*W*4,source+size_t(y)*surface->row_bytes(),W*4);surface->UnlockPixels();++frames;}
        }
#else
        if(overlay.state->failed)throw std::runtime_error("WebView2 failed; see diagnostics/renderer.log");
        overlay.capture();
        std::vector<uint8_t> png;ULONGLONG tick=0;
        {std::lock_guard lock(overlay.state->mutex);tick=overlay.state->tick;if(tick!=lastTick)png=overlay.state->png;}
        if(!png.empty()){auto frame=callayers::DecodeOverlay(png,tick);if(frame){pixels=std::move(frame->pixels);lastTick=tick;++frames;}}
#endif
        // Both engines use identical CPU blending against a checkerboard.
        preview=pixels;
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){auto p=&preview[(size_t(y)*W+x)*4];
            unsigned bg=((x/32+y/32)%2)?65:35, inverse=255-p[3];
            for(int c=0;c<3;++c)p[c]=static_cast<unsigned char>(std::min(255u,unsigned(p[c])+(bg*inverse+127)/255));p[3]=255;
        }
        InvalidateRect(window,nullptr,FALSE);
        std::wstring title=
#ifdef TEST_ULTRALIGHT
            L"Ultralight CPU";
#else
            L"WebView2 PNG + CPU";
#endif
        title+=cards?L" — Three cards":L" — Empty page";
        title+=L" — Space: toggle cards — Frames: "+std::to_wstring(frames);
        SetWindowTextW(window,title.c_str());
    }
};
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
    auto test=reinterpret_cast<Test*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){test=static_cast<Test*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(test));}
    if(!test)return DefWindowProcW(window,message,wParam,lParam);
    switch(message){
    case WM_TIMER:try{test->tick(window);}catch(const std::exception& error){KillTimer(window,1);test->failed=true;MessageBoxA(window,error.what(),"Renderer test stopped",MB_ICONERROR);}return 0;
    case WM_KEYDOWN:if(wParam==VK_SPACE)test->toggle();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
        BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=W;bitmap.bmiHeader.biHeight=-H;bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
        FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const double scale=std::min(double(bounds.right)/W,double(bounds.bottom)/H);
        const int width=int(W*scale),height=int(H*scale);
        SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
        StretchDIBits(dc,(bounds.right-width)/2,(bounds.bottom-height)/2,width,height,0,0,W,H,test->preview.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);EndPaint(window,&paint);return 0;}
    case WM_DESTROY:KillTimer(window,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try{winrt::init_apartment(winrt::apartment_type::single_threaded);
        {Test test;WNDCLASSW cls{};cls.lpfnWndProc=WindowProc;cls.hInstance=instance;cls.lpszClassName=L"CallayersRendererTest";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
        RECT area{0,0,W,H};AdjustWindowRectExForDpi(&area,WS_OVERLAPPEDWINDOW,FALSE,0,GetDpiForSystem());
        auto window=CreateWindowW(cls.lpszClassName,L"Opening renderer…",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,area.right-area.left,area.bottom-area.top,nullptr,nullptr,instance,&test);
        if(!window)throw std::runtime_error("Cannot create test window");
        test.start(instance);ShowWindow(window,show);SetTimer(window,1,33,nullptr);
        MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}}
        winrt::uninit_apartment();return 0;
    }catch(const std::exception& error){MessageBoxA(nullptr,error.what(),"Renderer test startup failed",MB_ICONERROR);return 1;}
    catch(...){MessageBoxW(nullptr,L"Renderer startup failed.",L"Renderer test",MB_ICONERROR);return 1;}
}
