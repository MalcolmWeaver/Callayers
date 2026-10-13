#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <mferror.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include "capture.h"
#include "overlay.h"
#include "frame_bridge.h"
#include "resource_diagnostics.h"
#include "workspace.h"
#include <Microsoft.UI.Dispatching.Interop.h>
#include <winrt/base.h>

using namespace callayers;
namespace {
struct Card {
    double x, y, w, h;
    bool visible = true;
    // The HTML document keeps this layout size for its whole lifetime. Resizing
    // changes presentation scale, so scripts/animation and text layout survive.
    double contentW = w, contentH = h;
};
struct Point { double x, y; };
struct PresentedFrame { std::vector<uint8_t> pixels; bool live = false; ULONGLONG tick = 0; unsigned version = 0; };
struct App {
    HWND window = nullptr;
    std::unique_ptr<Workspace> workspace;
    HFONT font = nullptr, titleFont = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(29, 29, 33));
    HDC backDC = nullptr; HBITMAP backBitmap = nullptr; HGDIOBJ oldBitmap = nullptr;
    int backWidth = 0, backHeight = 0;
    FrameBridge bridge;
    Overlay overlay;
    std::vector<CameraDevice> devices;
    int selectedCamera = -1;
    std::mutex commandMutex, previewMutex;
    std::wstring wantedIdentity;
    std::atomic<unsigned> commandVersion{1};
    std::atomic<bool> demand{false}, quitting{false};
    std::atomic<HRESULT> captureError{S_OK};
    std::atomic<bool> opening{false};
    std::thread worker;
    std::shared_ptr<const PresentedFrame> preview;
    Card cards[3]{{48, 540, 420, 128}, {870, 62, 340, 148}, {902, 470, 300, 180}};
    std::vector<std::vector<Point>> strokes{{{70, 495}, {175, 475}, {280, 495}, {390, 470}}};
    bool penMode = false, dragging = false, resizing = false, drawing = false;
    int selectedCard = -1;
    Point dragStart{}; Card original{};
    RECT previewRect{};
    float dpi = 1;
    std::wstring status;
    int px(double value) const { return int(std::lround(value * dpi)); }
    ~App() {
        if (oldBitmap) SelectObject(backDC, oldBitmap);
        if (backBitmap) DeleteObject(backBitmap);
        if (backDC) DeleteDC(backDC);
        if (font) DeleteObject(font); if (titleFont) DeleteObject(titleFont); DeleteObject(background);
    }
    void invalidateOutput() { bridge.stop(); }
    std::wstring scene() const {
        std::wostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(2) << L"{\"cards\":[";
        for (int i = 0; i < 3; ++i) {
            if (i) out << ',';
            const auto& c = cards[i];
            out << L"{\"id\":" << i << L",\"x\":" << c.x << L",\"y\":" << c.y << L",\"w\":" << c.w << L",\"h\":" << c.h << L",\"contentW\":" << c.contentW << L",\"contentH\":" << c.contentH << L",\"visible\":" << (c.visible ? L"true" : L"false") << '}';
        }
        out << L"],\"strokes\":[";
        for (size_t i = 0; i < strokes.size(); ++i) {
            if (i) out << ','; out << '[';
            for (size_t j = 0; j < strokes[i].size(); ++j) { if (j) out << ','; out << '[' << strokes[i][j].x << ',' << strokes[i][j].y << ']'; }
            out << ']';
        }
        out << L"]}"; return out.str();
    }
    void syncCards() { if(workspace) { bool visible[3]{cards[0].visible,cards[1].visible,cards[2].visible}; workspace->card(selectedCard,visible); } }
    void changed() { overlay.update(scene()); syncCards(); InvalidateRect(window, nullptr, FALSE); }
    void enumerate(bool initial = false) {
        std::wstring previous; { std::lock_guard lock(commandMutex); previous=wantedIdentity; }
        devices = EnumerateCameras(); selectedCamera = -1;
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].identity == previous) selectedCamera = int(i);
        }
        if (initial) {
            int matches = 0;
            for (size_t i = 0; i < devices.size(); ++i) if (devices[i].name == L"USB Camera") { selectedCamera = int(i); ++matches; }
            if (matches != 1) selectedCamera = -1;
        }
        std::vector<std::wstring> names; for(const auto& device:devices)names.push_back(device.name);
        if(workspace)workspace->cameras(names,selectedCamera);
        choose();
    }
    void choose() {
        invalidateOutput();
        { std::lock_guard lock(previewMutex); preview.reset(); }
        { std::lock_guard lock(commandMutex); wantedIdentity = selectedCamera >= 0 ? devices[selectedCamera].identity : L""; }
        captureError = S_OK; ++commandVersion;
        updateDemand();
    }
    void updateDemand() {
        bool wanted = selectedCamera >= 0 && (!IsIconic(window) || bridge.consumerActive());
        bool was = demand.exchange(wanted);
        if (was && !wanted) invalidateOutput();
        overlay.enable(wanted);
    }
    void buildFonts() {
        if (font) DeleteObject(font); if (titleFont) DeleteObject(titleFont);
        font = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        titleFont = CreateFontW(-px(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    void layout() {
        RECT client; GetClientRect(window, &client);
        int left = px(324), top = px(96+(workspace?workspace->toolbarHeight():0)), availableW = std::max<int>(1, client.right - left - px(24)), availableH = std::max<int>(1, client.bottom - top - px(24));
        double scale = std::min(double(availableW) / Width, double(availableH) / Height);
        int w = int(Width * scale), h = int(Height * scale);
        previewRect = {left + (availableW-w)/2, top + (availableH-h)/2, left + (availableW+w)/2, top + (availableH+h)/2};
        if(workspace)workspace->layout(client.right,client.bottom,dpi,previewRect);
        InvalidateRect(window, nullptr, FALSE);
    }
    Point canvasPoint(int x, int y) const {
        return {std::clamp(double(x - previewRect.left) * Width / std::max(1L, previewRect.right-previewRect.left), 0.0, double(Width)),
                std::clamp(double(y - previewRect.top) * Height / std::max(1L, previewRect.bottom-previewRect.top), 0.0, double(Height))};
    }
    RECT cardRect(const Card& c) const {
        double sx = double(previewRect.right-previewRect.left) / Width, sy = double(previewRect.bottom-previewRect.top) / Height;
        return {previewRect.left + int(c.x*sx), previewRect.top + int(c.y*sy), previewRect.left+int((c.x+c.w)*sx), previewRect.top+int((c.y+c.h)*sy)};
    }
    void pointerDown(int x, int y) {
        POINT at{x, y}; if (!PtInRect(&previewRect, at)) return;
        SetFocus(window);
        Point p = canvasPoint(x, y);
        if (penMode) {
            if (strokes.size() >= 200) return;
            strokes.push_back({p, {p.x+0.1, p.y+0.1}}); drawing = true; selectedCard = -1;
        } else {
            selectedCard = -1;
            for (int i = 2; i >= 0; --i) {
                const auto& c = cards[i];
                if (c.visible && p.x >= c.x && p.x <= c.x+c.w && p.y >= c.y && p.y <= c.y+c.h) {
                    selectedCard = i; original = c; dragStart = p; dragging = true;
                    resizing = p.x > c.x+c.w-28 && p.y > c.y+c.h-28; break;
                }
            }
        }
        SetCapture(window); changed();
    }
    void pointerMove(int x, int y) {
        if (!drawing && !dragging) return;
        Point p = canvasPoint(x, y);
        if (drawing && !strokes.empty() && strokes.back().size() < 4096) {
            auto last = strokes.back().back(); if (std::hypot(p.x-last.x, p.y-last.y) < 2) return;
            strokes.back().push_back(p);
        }
        if (dragging && selectedCard >= 0) {
            auto& c = cards[selectedCard];
            if (resizing) {
                // Project the corner drag onto the card's diagonal. One scale
                // factor preserves aspect ratio even on horizontal/vertical drags.
                double factor=1+((p.x-dragStart.x)*original.w+(p.y-dragStart.y)*original.h)/(original.w*original.w+original.h*original.h);
                double maximum=std::min((Width-c.x)/original.w,(Height-c.y)/original.h);
                double minimum=std::min(maximum,std::max(180.0/original.w,100.0/original.h));
                factor=std::clamp(factor,minimum,maximum);
                c.w=original.w*factor; c.h=original.h*factor;
            }
            else { c.x = std::clamp(original.x+p.x-dragStart.x, 0.0, Width-c.w); c.y = std::clamp(original.y+p.y-dragStart.y, 0.0, Height-c.h); }
        }
        changed();
    }
    void text(HDC dc, const std::wstring& value, RECT rect, UINT flags = DT_LEFT | DT_WORDBREAK) { DrawTextW(dc, value.c_str(), -1, &rect, flags); }
    void paint() {
        PAINTSTRUCT ps; HDC target = BeginPaint(window, &ps); RECT client; GetClientRect(window, &client);
        if (!backDC) backDC = CreateCompatibleDC(target);
        if (!backBitmap || backWidth != client.right || backHeight != client.bottom) {
            if (oldBitmap) SelectObject(backDC, oldBitmap);
            if (backBitmap) DeleteObject(backBitmap);
            backWidth = std::max(1L, client.right); backHeight = std::max(1L, client.bottom);
            backBitmap = CreateCompatibleBitmap(target, backWidth, backHeight); oldBitmap = SelectObject(backDC, backBitmap);
        }
        HDC dc = backDC; FillRect(dc, &client, background); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(237,241,249));
        std::shared_ptr<const PresentedFrame> frame;
        { std::lock_guard lock(previewMutex); frame = preview; }
        bool fresh = frame && frame->live && frame->version == commandVersion && GetTickCount64()-frame->tick < FreshMs && demand && SUCCEEDED(captureError.load());
        FillRect(dc, &previewRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (fresh) {
            BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=Width; info.bmiHeader.biHeight=-Height;
            info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
            StretchDIBits(dc, previewRect.left, previewRect.top, previewRect.right-previewRect.left, previewRect.bottom-previewRect.top,
                0,0,Width,Height,frame->pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
            if (selectedCard >= 0 && cards[selectedCard].visible) {
                RECT r = cardRect(cards[selectedCard]); HPEN outline = CreatePen(PS_SOLID, px(2), RGB(109,211,255));
                auto oldPen = SelectObject(dc, outline), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Rectangle(dc, r.left, r.top, r.right, r.bottom);
                SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(outline);
                RECT handle{r.right-px(12),r.bottom-px(12),r.right,r.bottom}; HBRUSH b=CreateSolidBrush(RGB(109,211,255)); FillRect(dc,&handle,b); DeleteObject(b);
            }
        }
        BitBlt(target,0,0,client.right,client.bottom,dc,0,0,SRCCOPY); EndPaint(window,&ps);
    }
    void refreshWorkspace() {
        std::shared_ptr<const PresentedFrame> frame;
        { std::lock_guard lock(previewMutex); frame=preview; }
        HRESULT error=captureError.load();
        bool fresh=frame && frame->live && frame->version==commandVersion && GetTickCount64()-frame->tick<FreshMs && demand && SUCCEEDED(error) && !overlay.state->failed;
        std::wstring message, state;
        if(selectedCamera<0) {state=L"Choose your webcam";message=devices.empty()?L"Connect your webcam, then choose Retry.":L"Choose your webcam.\nYour camera and cards will appear here.";}
        else if(overlay.state->failed) {state=L"Cards unavailable";message=L"We couldn't show your cards. Choose Retry to restart them.";}
        else if(error==E_ACCESSDENIED) {state=L"Camera unavailable";message=L"Windows is blocking camera access. Allow camera access for desktop apps in Windows settings, then Retry.";}
        else if(error==HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)) {state=L"Camera unavailable";message=L"Your webcam is being used by another app. Close that app's camera preview, then Retry.";}
        else if(FAILED(error)) {state=L"Camera unavailable";message=L"We couldn't get a picture from this webcam. Close other camera previews or reconnect it, then Retry.";}
        else if(!fresh) {state=L"Opening your webcam";message=opening?L"Opening your webcam…":L"Waiting for a picture. If it doesn't appear, choose Retry.";}
        else state=bridge.consumerActive()?L"Video is reaching your calling app":L"Ready for your call";
        if(workspace)workspace->state(state,message,fresh,error==E_ACCESSDENIED);
    }
    static void compose(const CameraImage& camera, const OverlayImage& overlay, std::vector<uint8_t>& output) {
        StageTimer timing(VideoStage::Compose);
        output.assign(PixelBytes,0);
        double scale=std::min(double(Width)/camera.width,double(Height)/camera.height);
        int w=std::max(1,int(camera.width*scale)), h=std::max(1,int(camera.height*scale));
        int left=(Width-w)/2, top=(Height-h)/2;
        for(int y=0;y<h;++y) {
            UINT sourceY=std::min(camera.height-1,UINT(y/scale));
            auto dst=output.data()+(size_t(y+top)*Width+left)*4;
            auto row=camera.pixels.data()+size_t(sourceY)*camera.width*4;
            if (w == int(camera.width)) memcpy(dst, row, size_t(w)*4);
            else for(int x=0;x<w;++x) { UINT sourceX=std::min(camera.width-1,UINT(x/scale)); memcpy(dst+x*4,row+sourceX*4,4); }
        }
        for(size_t i=0;i<PixelBytes;i+=4) {
            unsigned inverse=255-overlay.pixels[i+3];
            for(int c=0;c<3;++c) output[i+c]=uint8_t(std::min(255u,unsigned(overlay.pixels[i+c])+(unsigned(output[i+c])*inverse+127)/255));
            output[i+3]=255;
        }
    }
    void videoLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            Capture camera;
            unsigned version=0; bool started=false, attempted=false; LONG64 session=0;
            std::shared_ptr<OverlayImage> overlayFrame;
            auto next=std::chrono::steady_clock::now();
            while(!quitting) {
                unsigned requested=commandVersion;
                if(requested!=version || !demand) {
                    if(started || attempted) { bridge.stop(); camera.stop(); }
                    started=false; attempted=false; overlayFrame.reset(); version=requested;
                }
                if(demand && !attempted) {
                    std::wstring identity; {std::lock_guard lock(commandMutex);identity=wantedIdentity;}
                    attempted=true; opening=true;
                    HRESULT hr=identity.empty()?E_INVALIDARG:camera.start(identity);
                    opening=false;
                    if(version!=commandVersion || !demand || quitting) {camera.stop();attempted=false;continue;}
                    captureError=hr; started=SUCCEEDED(hr);
                    if(started) session=bridge.begin();
                }
                if(started && demand) {
                    if(FAILED(camera.state->error.load())) {captureError=camera.state->error.load();bridge.stop();camera.stop();started=false;}
                    else {
                        std::vector<uint8_t> png; ULONGLONG tick=0; unsigned epoch=0;
                        {std::lock_guard lock(overlay.state->mutex);png.swap(overlay.state->png);tick=overlay.state->tick;epoch=overlay.state->epoch;}
                        if(!png.empty() && epoch==overlay.state->currentEpoch) {
                            auto decoded=DecodeOverlay(png,tick);
                            if(decoded && epoch==overlay.state->currentEpoch) overlayFrame=std::move(decoded);
                        }
                        std::shared_ptr<const CameraImage> image;
                        {std::lock_guard lock(camera.state->mutex);image=camera.state->latest;}
                        ULONGLONG now=GetTickCount64();
                        bool fresh=image && overlayFrame && now-image->tick<FreshMs && now-overlayFrame->tick<FreshMs && !overlay.state->failed;
                        if(fresh && version==commandVersion && demand && !quitting) {
                            auto result=std::make_shared<PresentedFrame>();
                            compose(*image,*overlayFrame,result->pixels); result->live=true; result->version=version;
                            result->tick=std::min(image->tick,overlayFrame->tick);
                            if(version==commandVersion && demand && !quitting) {
                                { StageTimer timing(VideoStage::Publish); bridge.publish(result->pixels,session,result->tick); }
                                std::lock_guard lock(previewMutex);preview=std::move(result);
                            }
                        } else {
                            // Let the last publication expire. A producer stall must
                            // not refresh its timestamp merely by recompositing it.
                            std::lock_guard lock(previewMutex);preview.reset();
                        }
                    }
                }
                next+=std::chrono::nanoseconds(1000000000/Fps);
                auto now=std::chrono::steady_clock::now();if(next<now)next=now;
                std::this_thread::sleep_until(next);
            }
            bridge.stop();camera.stop();
        }
        CoUninitialize();
    }
};
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,message,wParam,lParam);
    switch(message) {
    case WM_CREATE: {
        try {
            WorkspaceActions actions;
            actions.camera=[app](int index){if(index<0 || index>=int(app->devices.size()))return;app->selectedCamera=index;app->choose();std::vector<std::wstring> names;for(const auto& d:app->devices)names.push_back(d.name);app->workspace->cameras(names,index);};
            actions.selectCard=[app](int index){app->selectedCard=index;app->penMode=false;app->workspace->penMode(false);app->syncCards();InvalidateRect(app->window,nullptr,FALSE);};
            actions.visibility=[app](int index,bool show){app->cards[index].visible=show;app->changed();};
            actions.pen=[app](bool pen){app->penMode=pen;app->dragging=app->drawing=false;ReleaseCapture();};
            actions.retry=[app]{if(app->overlay.state->failed)app->overlay.retry(GetModuleHandleW(nullptr));app->enumerate();};
            actions.privacy=[] {ShellExecuteW(nullptr,L"open",L"ms-settings:privacy-webcam",nullptr,nullptr,SW_SHOWNORMAL);};
            actions.hideAll=[app]{for(auto& card:app->cards)card.visible=false;app->changed();};
            actions.clear=[app]{if(MessageBoxW(app->window,L"Clear all annotations? This cannot be undone.",L"Clear annotations",MB_YESNO|MB_ICONQUESTION)==IDYES){app->strokes.clear();app->changed();}};
            actions.quit=[app]{PostMessageW(app->window,WM_CLOSE,0,0);};
            actions.layout=[app]{app->layout();};
            app->workspace=std::make_unique<Workspace>(window,std::move(actions));
            app->dpi=GetDpiForWindow(window)/96.0f;app->buildFonts();app->layout();app->enumerate(true);app->syncCards();
            app->overlay.update(app->scene());app->overlay.initialize(GetModuleHandleW(nullptr));
            app->worker=std::thread([app]{app->videoLoop();});SetTimer(window,1,33,nullptr);return 0;
        } catch(winrt::hresult_error const& error) {Resources().event("workspace_controls_failed hresult="+std::to_string(error.code().value));MessageBoxW(window,L"Couldn't open the workspace. Close and reopen Callayers.",L"Callayers",MB_OK|MB_ICONERROR);return -1;}
        catch(...) {Resources().event("workspace_controls_failed");MessageBoxW(window,L"Couldn't open the workspace. Close and reopen Callayers.",L"Callayers",MB_OK|MB_ICONERROR);return -1;}
    }
    case WM_TIMER:app->updateDemand();app->overlay.capture();if(!IsIconic(window)){app->refreshWorkspace();InvalidateRect(window,nullptr,FALSE);}return 0;
    case WM_SIZE:if(app->workspace){app->layout();app->updateDemand();}return 0;
    case WM_DPICHANGED: {
        app->dpi=HIWORD(wParam)/96.0f;app->buildFonts();auto r=reinterpret_cast<RECT*>(lParam);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->layout();return 0;
    }
    case WM_GETMINMAXINFO: {auto limits=reinterpret_cast<MINMAXINFO*>(lParam);limits->ptMinTrackSize={app->px(1000),app->px(760)};return 0;}
    case WM_KEYDOWN:if(wParam==VK_TAB && app->workspace){app->workspace->focus((GetKeyState(VK_SHIFT)&0x8000)!=0);return 0;}break;
    case WM_LBUTTONDOWN:app->pointerDown(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));return 0;
    case WM_MOUSEMOVE:app->pointerMove(GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam));return 0;
    case WM_LBUTTONUP:app->dragging=app->drawing=false;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:app->dragging=app->drawing=false;return 0;
    case WM_ERASEBKGND:return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:SetTextColor(reinterpret_cast<HDC>(wParam),RGB(230,235,245));SetBkColor(reinterpret_cast<HDC>(wParam),RGB(18,22,31));return reinterpret_cast<LRESULT>(app->background);
    case WM_PAINT:app->paint();return 0;
    case WM_CLOSE:
        if(app->bridge.consumerActive() && MessageBoxW(window,L"Quit Callayers and stop video in your calling app?",L"Stop video and quit",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
        app->quitting=true;app->demand=false;app->bridge.stop();KillTimer(window,1);app->overlay.enable(false);app->workspace.reset();
        DestroyWindow(window);return 0;
    case WM_DESTROY:app->quitting=true;app->bridge.stop();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    HANDLE single=CreateMutexW(nullptr,TRUE,L"Local\\Callayers.Foundation.v2.Desktop");
    if(!single || GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"Callayers is already open.",L"Callayers",MB_OK);if(single)CloseHandle(single);return 0;}
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com) || FAILED(MFStartup(MF_VERSION))){MessageBoxW(nullptr,L"Windows couldn't start the video services.",L"Callayers",MB_OK|MB_ICONERROR);CloseHandle(single);return 1;}
    InitCommonControls();
    int result=0;
    try {
        WorkspaceRuntime runtime;
        App app;
        if(!app.bridge.valid()){MessageBoxW(nullptr,L"Couldn't prepare Callayers video. Close and reopen the app.",L"Callayers",MB_OK|MB_ICONERROR);result=1;}
        else {
            WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=instance;wc.lpszClassName=L"CallayersFoundation";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&wc);
            HWND window=CreateWindowExW(0,wc.lpszClassName,L"Callayers",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1280,850,nullptr,nullptr,instance,&app);
            if(window){ShowWindow(window,show);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!ContentPreTranslateMessage(&message)){TranslateMessage(&message);DispatchMessageW(&message);}}}
            else {app.quitting=true;result=1;}
            if(app.worker.joinable())app.worker.join();
        }
    }
    catch(winrt::hresult_error const& error) {Resources().event("workspace_runtime_failed hresult="+std::to_string(error.code().value));MessageBoxW(nullptr,L"Couldn't start the Callayers workspace. Make sure Windows App Runtime 1.6 is installed, then reopen the app.",L"Callayers",MB_OK|MB_ICONERROR);result=1;}
    catch(...) {Resources().event("workspace_runtime_failed");MessageBoxW(nullptr,L"Couldn't start the Callayers workspace. Close and reopen the app.",L"Callayers",MB_OK|MB_ICONERROR);result=1;}
    MFShutdown();CoUninitialize();ReleaseMutex(single);CloseHandle(single);return result;
}
