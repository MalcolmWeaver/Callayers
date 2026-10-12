#include "common.h"
#include "camera_demand.h"
#include "tray_icon.h"
#include "card_host.h"
#include "text_editor_ui.h"
#include "winui_shell.h"
#include <commctrl.h>
#include <fstream>

using namespace callayers;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::Make;
namespace {
void StartupDiagnostic(const std::wstring& message){
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
    std::ofstream log(std::filesystem::path(path).parent_path()/L"Callayers-startup.log",std::ios::app);log<<winrt::to_string(message)<<"\n";
}
struct Frames {
    std::atomic<std::shared_ptr<Frame>> latest{Pattern(0,true)};
    std::atomic<std::shared_ptr<Frame>> composed{Pattern(0,true)};
    std::atomic<std::shared_ptr<const TextOverlay>> overlay;
    std::shared_ptr<CardHost> cards;
    std::atomic<bool> demanded{false};
    std::atomic<ULONGLONG> lastFrameRequest{0};
    std::atomic<int> mode{1};
    std::atomic<HRESULT> error{S_OK};
    std::atomic<uint64_t> sequence{0};
    std::shared_ptr<Frame> idle=Pattern(0,true);
    std::shared_ptr<Frame> Current() {
        auto f=composed.load();
        return GetTickCount64()-f->header.capturedMs>750?idle:f;
    }
};
class Publisher {
    std::shared_ptr<Frames> frames;
    Handle stop{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Handle pipe;
    std::thread worker,pattern;
public:
    explicit Publisher(std::shared_ptr<Frames> f):frames(std::move(f)) {
        // Only this user, service identities, and administrators can access the local pipe.
        Handle token;Check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        DWORD bytes=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes);
        std::vector<BYTE> info(bytes);Check(GetTokenInformation(token.value,TokenUser,info.data(),bytes,&bytes)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        wchar_t* sid=nullptr;Check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid,&sid)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        std::wstring sddl=L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;LS)(A;;GRGW;;;";sddl+=sid;sddl+=L")";LocalFree(sid);
        PSECURITY_DESCRIPTOR sd=nullptr;
        Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
        pipe.reset(CreateNamedPipeW(PipeName,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,4096,0,&sa));
        DWORD error=GetLastError();LocalFree(sd);if(!pipe)throw HRESULT_FROM_WIN32(error);
        worker=std::thread([this]{Serve();});
        pattern=std::thread([this]{
            auto due=Clock::now();
            while(WaitForSingleObject(stop.value,0)!=WAIT_OBJECT_0) {
                if(!frames->demanded){WaitForSingleObject(stop.value,33);due=Clock::now();continue;}
                if(frames->mode==1)frames->latest=Pattern(++frames->sequence);
                auto background=frames->latest.load();
                if(GetTickCount64()-background->header.capturedMs>750)background=frames->idle;
                if(frames->cards)if(auto card=frames->cards->Latest())background=ComposeText(*background,*card);
                auto overlay=frames->overlay.load();
                auto output=overlay?ComposeText(*background,*overlay):std::make_shared<Frame>(*background);
                output->header.capturedMs=GetTickCount64();
                frames->composed=std::move(output);
                due+=std::chrono::nanoseconds(1'000'000'000/Fps);
                auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(due-Clock::now()).count();
                if(remaining>0)WaitForSingleObject(stop.value,static_cast<DWORD>(remaining));else due=Clock::now();
            }
        });
    }
    ~Publisher(){SetEvent(stop.value);if(worker.joinable())worker.join();if(pattern.joinable())pattern.join();}
private:
    void Serve() {
        while(WaitForSingleObject(stop.value,0)!=WAIT_OBJECT_0) {
            Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED ov{};ov.hEvent=event.value;
            BOOL connected=ConnectNamedPipe(pipe.value,&ov);DWORD error=GetLastError(),count=0;
            if(!connected&&error==ERROR_IO_PENDING) {
                HANDLE waits[]={event.value,stop.value};
                if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)!=WAIT_OBJECT_0) {
                    CancelIoEx(pipe.value,&ov);GetOverlappedResult(pipe.value,&ov,&count,TRUE);break;
                }
                connected=GetOverlappedResult(pipe.value,&ov,&count,FALSE);
            }else if(!connected&&error==ERROR_PIPE_CONNECTED)connected=TRUE;
            if(connected) {
                // Older registered source DLLs do not hold a demand lease. A
                // frame request is still evidence that a camera consumer exists.
                frames->lastFrameRequest=GetTickCount64();
                auto f=frames->Current();FrameHeader header=f->header;
                // Idle frames remain valid replies, but are visibly distinct from live frames.
                if(header.mode==0)header.capturedMs=GetTickCount64();
                if(Transfer(pipe.value,true,&header,sizeof(header),stop.value,250)&&
                    Transfer(pipe.value,true,f->pixels.data(),FrameBytes,stop.value,250)) {
                    BYTE ack=0;Transfer(pipe.value,false,&ack,1,stop.value,250);
                }
            }
            DisconnectNamedPipe(pipe.value);
        }
    }
};

class Capture final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,IMFSourceReaderCallback> {
    std::mutex mutex;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaSource> source;
    std::shared_ptr<Frames> frames;
    bool stopped=false;
    UINT width=0,height=0;LONG stride=0;
public:
    HRESULT Open(const Device& device,std::shared_ptr<Frames> f) {
        frames=std::move(f);
        try {
            source=OpenCamera(device);
            ComPtr<IMFAttributes> a;Check(MFCreateAttributes(&a,3));
            Check(a->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,this));
            Check(a->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE));
            Check(a->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS,FALSE));
            Check(MFCreateSourceReaderFromMediaSource(source.Get(),a.Get(),&reader));
            Check(reader->SetStreamSelection(AllStreams,FALSE));
            Check(reader->SetStreamSelection(VideoStream,TRUE));
            ComPtr<IMFMediaType> best;uint64_t bestScore=0;
            for(DWORD i=0;;++i) {
                ComPtr<IMFMediaType> t;if(FAILED(reader->GetNativeMediaType(VideoStream,i,&t)))break;
                UINT w=0,h=0,n=0,d=1;MFGetAttributeSize(t.Get(),MF_MT_FRAME_SIZE,&w,&h);MFGetAttributeRatio(t.Get(),MF_MT_FRAME_RATE,&n,&d);
                if(!w||!h||!d||w>Width||h>Height||n/d<15)continue;
                uint64_t score=uint64_t(w)*h*100+std::min(n/d,30u);
                if(score>bestScore){best=t;bestScore=score;}
            }
            if(best)Check(reader->SetCurrentMediaType(VideoStream,nullptr,best.Get()));
            ComPtr<IMFMediaType> output;Check(MFCreateMediaType(&output));
            Check(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
            Check(output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32));
            Check(reader->SetCurrentMediaType(VideoStream,nullptr,output.Get()));
            UpdateFormat();
            return reader->ReadSample(VideoStream,0,nullptr,nullptr,nullptr,nullptr);
        }catch(HRESULT hr){Close();return hr;}catch(...){Close();return E_OUTOFMEMORY;}
    }
    void Close() {
        ComPtr<IMFSourceReader> old;ComPtr<IMFMediaSource> device;
        {std::lock_guard lock(mutex);stopped=true;old=reader;reader.Reset();device=source;source.Reset();}
        if(old)old->Flush(AllStreams);
        if(device)device->Shutdown();
    }
    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT hr,DWORD,DWORD flags,LONGLONG,IMFSample* sample) override {
        std::lock_guard lock(mutex);if(stopped)return S_OK;
        try {
            Check(hr);if(flags&MF_SOURCE_READERF_ERROR)throw E_FAIL;
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM)throw MF_E_END_OF_STREAM;
            if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)UpdateFormat();
            if(sample) {
                ComPtr<IMFMediaBuffer> buffer;Check(sample->ConvertToContiguousBuffer(&buffer));
                ComPtr<IMF2DBuffer> twoD;BYTE* data=nullptr;LONG pitch=stride;
                std::shared_ptr<Frame> frame;
                if(SUCCEEDED(buffer.As(&twoD))) {
                    Check(twoD->Lock2D(&data,&pitch));
                    try {frame=FromBgra(data,pitch,width,height,++frames->sequence);}catch(...){twoD->Unlock2D();throw;}
                    twoD->Unlock2D();
                }else{
                    DWORD length=0;Check(buffer->Lock(&data,nullptr,&length));
                    if(length<static_cast<uint64_t>(std::abs(pitch))*height){buffer->Unlock();throw MF_E_BUFFERTOOSMALL;}
                    if(pitch<0)data+=static_cast<ptrdiff_t>(height-1)*-pitch;
                    try{frame=FromBgra(data,pitch,width,height,++frames->sequence);}catch(...){buffer->Unlock();throw;}
                    buffer->Unlock();
                }
                if(frames->mode==2)frames->latest=std::move(frame);
            }
            Check(reader->ReadSample(VideoStream,0,nullptr,nullptr,nullptr,nullptr));
        }catch(HRESULT error){frames->error=error;}catch(...){frames->error=E_OUTOFMEMORY;}
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD,IMFMediaEvent*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override{return S_OK;}
private:
    void UpdateFormat(){
        ComPtr<IMFMediaType> t;Check(reader->GetCurrentMediaType(VideoStream,&t));
        Check(MFGetAttributeSize(t.Get(),MF_MT_FRAME_SIZE,&width,&height));
        UINT32 strideValue=0;if(SUCCEEDED(t->GetUINT32(MF_MT_DEFAULT_STRIDE,&strideValue)))stride=static_cast<LONG>(strideValue);
        else Check(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1,width,&stride));
    }
};

// Keep the background and scaled video offscreen until the whole paint is ready.
// A single blit prevents the intermediate white erase from reaching the display.
class PreviewBuffer {
    HDC dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ original=nullptr;
    int width=0,height=0;
public:
    ~PreviewBuffer(){Reset();}
    PreviewBuffer()=default;
    PreviewBuffer(const PreviewBuffer&)=delete;
    PreviewBuffer& operator=(const PreviewBuffer&)=delete;
    HDC Prepare(HDC target,int w,int h){
        if(w<=0||h<=0)return nullptr;
        if(dc&&w==width&&h==height)return dc;
        Reset();
        dc=CreateCompatibleDC(target);
        if(!dc)return nullptr;
        bitmap=CreateCompatibleBitmap(target,w,h);
        if(!bitmap){Reset();return nullptr;}
        original=SelectObject(dc,bitmap);
        if(!original||original==HGDI_ERROR){original=nullptr;Reset();return nullptr;}
        width=w;height=h;
        return dc;
    }
private:
    void Reset(){
        if(dc&&original)SelectObject(dc,original);
        if(bitmap)DeleteObject(bitmap);
        if(dc)DeleteDC(dc);
        dc=nullptr;bitmap=nullptr;original=nullptr;width=height=0;
    }
};

struct App {
    HWND window=nullptr;
    std::shared_ptr<Frames> frames=std::make_shared<Frames>();
    std::unique_ptr<Publisher> publisher;
    std::unique_ptr<TextEditorUI> editor;
    std::unique_ptr<WinUIShell> shell;
    TrayIcon tray;
    std::unique_ptr<CameraDemandServer> demand;
    bool captureAttempted=false;
    int cameraIndex=0;
    bool shellOnly=false;
    ComPtr<Capture> capture;
    ComPtr<IMFVirtualCamera> virtualCamera;
    std::thread activation;
    std::atomic<bool> activating=false;
    std::atomic<HRESULT> activationResult=S_FALSE;
    std::vector<Device> devices;
    std::vector<BYTE> preview;
    PreviewBuffer paintBuffer;
    std::wstring cameraStatus=L"Camera output is off";
    RECT stage{};
    ~App(){if(capture)capture->Close();if(activation.joinable())activation.join();publisher.reset();}
    void StartOutput(){
        if(activating)return;
        if(activation.joinable())activation.join();
        if(virtualCamera)return;
        activating=true;activationResult=S_FALSE;cameraStatus=L"Starting virtual camera…";
        activation=std::thread([this]{
            CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            try{auto v=VirtualCamera();Check(v->Start(nullptr));virtualCamera=v;activationResult=S_OK;}
            catch(HRESULT hr){activationResult=hr;}catch(...){activationResult=E_FAIL;}
            activating=false;CoUninitialize();
        });
    }
    bool PreviewVisible()const{return window&&IsWindowVisible(window)&&!IsIconic(window);}
    void UpdateDemand(){
        const bool needed=NeedsCameraFrames(PreviewVisible(),demand?demand->Poll():0,frames->lastFrameRequest.load());
        frames->demanded=needed;
        if(!needed||cameraIndex<=0){
            if(capture){capture->Close();capture.Reset();}
            captureAttempted=false;
            if(!needed){frames->latest=frames->idle;frames->composed=frames->idle;}
        }else if(!captureAttempted){
            captureAttempted=true;frames->error=S_OK;frames->latest=frames->idle;
            capture=Make<Capture>();
            HRESULT hr=capture->Open(devices.at(cameraIndex-1),frames);
            if(FAILED(hr)){frames->error=hr;capture.Reset();}
        }
        if(frames->cards)frames->cards->SetActive(needed);
    }
    void Select(){
        if(capture){capture->Close();capture.Reset();}
        captureAttempted=false;frames->error=S_OK;frames->latest=frames->idle;
        frames->mode=cameraIndex<=0?1:2;
        UpdateDemand();
    }
    void Layout(){
        RECT r;GetClientRect(window,&r);const double dpi=GetDpiForWindow(window)/96.;const int margin=static_cast<int>(24*dpi),left=static_cast<int>(300*dpi),top=static_cast<int>(((shell?shell->HeaderHeight():72)+24)*dpi),bottom=static_cast<int>(24*dpi);
        if(shell)shell->Layout(r.right,r.bottom,dpi);
        const int availableW=std::max(1L,r.right-left-2*margin),availableH=std::max(1L,r.bottom-top-bottom);
        const double scale=std::min(double(availableW)/Width,double(availableH)/Height);
        int w=static_cast<int>(Width*scale),h=static_cast<int>(Height*scale);
        int y=top+(availableH-h)/2;stage={left+(r.right-left-w)/2,y,left+(r.right-left+w)/2,y+h};
        if(editor)editor->SetStage(stage);
        InvalidateRect(window,nullptr,FALSE);
    }
    void Tick(){
        UpdateDemand();
        if(editor)editor->TickInk();
        if(shellOnly&&frames->demanded){
            if(frames->mode==1)frames->latest=Pattern(++frames->sequence);
            auto background=frames->latest.load();if(GetTickCount64()-background->header.capturedMs>750)background=frames->idle;
            if(frames->cards)if(auto card=frames->cards->Latest())background=ComposeText(*background,*card);
            auto overlay=frames->overlay.load();frames->composed=overlay?ComposeText(*background,*overlay):std::make_shared<Frame>(*background);
        }
        HRESULT result=activationResult.exchange(S_FALSE);
        if(result==S_OK)cameraStatus=L"Callayers is available in meeting apps — 1920 × 1080, 30 fps.";
        else if(FAILED(result)){
            cameraStatus=L"Cannot start camera: "+ErrorText(result);
            if(result==REGDB_E_CLASSNOTREG)cameraStatus+=L" Run the development registration script first.";
            else if(result==MF_E_SHUTDOWN)cameraStatus+=L" The camera source shut down during startup. Update the camera component and restart Callayers.";
            else if(result==E_ACCESSDENIED)cameraStatus+=L" Check Windows camera privacy permissions.";
        }
        auto frame=frames->Current();
        std::wstring message=cameraStatus;
        HRESULT error=frames->error.load();
        if(FAILED(error))message=L"Webcam unavailable: "+ErrorText(error)+L" Choose Test pattern or retry the webcam.";
        else if(frames->mode==2&&frame->header.mode==0)message=L"Waiting for webcam frames — idle image is being sent.";
        if(frames->cards&&FAILED(frames->cards->Error()))message=L"Card rendering failed: "+ErrorText(frames->cards->Error())+L". Save your deck and restart Callayers.";
        if(shell)shell->Status(message,activating,!activating&&virtualCamera!=nullptr);
        if(PreviewVisible()){
            if(editor&&editor->IsEditing()){
                auto background=frames->latest.load();
                if(GetTickCount64()-background->header.capturedMs>750)background=frames->idle;
                if(frames->cards)if(auto card=frames->cards->Latest())background=ComposeText(*background,*card);
                frame=editor->EditingFrame(*background);
            }
            ToBgra(*frame,preview);
            InvalidateRect(window,&stage,FALSE);
        }
    }
};
LRESULT CALLBACK WindowProc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);app->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(window,msg,wp,lp);
    try{
    static const UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    if(msg==taskbarCreated){if(!app->tray.Recreate(window))TrayIcon::Open(window);return 0;}
    if(app->editor){for(const auto& change:app->frames->cards->TakeStates())app->editor->ApplyCardState(change.id,change.epoch,change.html,change.state);LRESULT result;if(app->editor->Message(msg,wp,lp,result))return result;}
    switch(msg){
    case WM_CREATE:{
        app->editor=std::make_unique<TextEditorUI>(window,[app](auto overlay){app->frames->overlay=std::move(overlay);},false);
        app->editor->SceneChanged=[app](const Scene& scene){app->frames->cards->SetScene(scene,app->editor->DocumentEpoch());};
        app->editor->CardMouse=[app](UINT msg,POINT point,WPARAM buttons){app->frames->cards->Mouse(msg,point,buttons);};
        app->editor->CardKey=[app](UINT msg,WPARAM key,LPARAM flags){app->frames->cards->Key(msg,key,flags);};
        app->editor->CardText=[app](const std::wstring& text){app->frames->cards->InsertText(text);};
        app->editor->FlushCardState=[app]{if(!app->frames->cards->FlushState())throw std::runtime_error("The card is still responding. Please try saving again.");for(const auto& change:app->frames->cards->TakeStates())app->editor->ApplyCardState(change.id,change.epoch,change.html,change.state);};
        app->shell=std::make_unique<WinUIShell>(window,*app->editor,app->devices,[app](int index){app->cameraIndex=index;app->Select();},[app]{app->StartOutput();});
        SetTimer(window,1,33,nullptr);return 0;}
    case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lp);double dpi=GetDpiForWindow(window)/96.;info->ptMinTrackSize={static_cast<LONG>(1000*dpi),static_cast<LONG>(600*dpi)};return 0;}
    case WM_DPICHANGED:{auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->Layout();return 0;}
    case WM_SIZE:app->Layout();app->UpdateDemand();return 0;
    case WM_SHOWWINDOW:PostMessageW(window,WM_TIMER,1,0);break;
    case TrayMessage:app->tray.Event(window,lp);app->UpdateDemand();return 0;
    case WM_TIMER:if(wp==2){KillTimer(window,2);SendMessageW(window,QuitMessage,0,0);return 0;}app->Tick();return 0;
    case WM_ERASEBKGND:return 1; // WM_PAINT presents the complete buffered background.
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC target=BeginPaint(window,&ps);
        RECT client;GetClientRect(window,&client);
        HDC dc=app->paintBuffer.Prepare(target,client.right,client.bottom);
        if(dc){
            HBRUSH background=CreateSolidBrush(RGB(28,28,32));FillRect(dc,&client,background);DeleteObject(background);
            FillRect(dc,&app->stage,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if(!app->preview.empty()){
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-static_cast<LONG>(Height);
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
            auto r=app->stage;StretchDIBits(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,0,0,Width,Height,app->preview.data(),&info,DIB_RGB_COLORS,SRCCOPY);
            }
            if(app->editor)app->editor->PaintAdorners(dc);
            const auto& r=ps.rcPaint;
            BitBlt(target,r.left,r.top,r.right-r.left,r.bottom-r.top,dc,r.left,r.top,SRCCOPY);
        }
        EndPaint(window,&ps);if(app->editor)app->editor->RefreshEdit();return 0;}
    case WM_CLOSE:
        if(app->tray.Add(window)){ShowWindow(window,SW_HIDE);app->UpdateDemand();}
        else MessageBoxW(window,L"Could not add the tray icon. Callayers will stay open so you can reach it.",L"Callayers",MB_OK|MB_ICONERROR);
        return 0;
    case QuitMessage:
        TrayIcon::Open(window);
        if(app->editor&&!app->editor->MayDiscard())return 0;
        app->tray.Remove();app->shell.reset();DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);PostQuitMessage(0);return 0;
    }
    }catch(...){
        try{throw;}catch(const winrt::hresult_error& e){StartupDiagnostic(e.message().c_str());}catch(HRESULT hr){StartupDiagnostic(ErrorText(hr));}catch(...){StartupDiagnostic(L"Native window initialization failed");}
        if(!app->shellOnly)EditorError(window);return msg==WM_CREATE?-1:0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show){
    if(!wcsstr(command,L"--headless")&&!wcsstr(command,L"--shell-only")){
        if(HWND existing=FindWindowW(L"CallayersEditor",nullptr)){TrayIcon::Open(existing);return 0;}
    }
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(com))return 1;
    HRESULT startup=MFStartup(MF_VERSION);if(FAILED(startup)){CoUninitialize();return 2;}
    int result=0;
    bool bootstrapped=false;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueController dispatcher{nullptr};
    ux::Application xamlApplication{nullptr};hosting::WindowsXamlManager xamlManager{nullptr};
    try {
        if(!wcsstr(command,L"--headless")){
            PACKAGE_VERSION minimum{};Check(MddBootstrapInitialize(0x00010006,L"",minimum));bootstrapped=true;
            dispatcher=winrt::Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
            xamlApplication=winrt::make<IslandApplication>();xamlManager=hosting::WindowsXamlManager::InitializeForCurrentThread();
            xamlApplication.Resources().MergedDictionaries().Append(controls::XamlControlsResources());
        }
        App app;app.frames->cards=std::make_shared<CardHost>();app.shellOnly=wcsstr(command,L"--shell-only")!=nullptr;if(!app.shellOnly)app.publisher=std::make_unique<Publisher>(app.frames);app.devices=Cameras();if(!app.shellOnly)app.demand=std::make_unique<CameraDemandServer>();
        if(wcsstr(command,L"--demo-text")){
            Scene scene;TextElement text;text.id=NewId();text.text=L"Callayers text test";text.x=.05;text.y=.12;text.size=72;scene.texts.push_back(text);
            TextRenderer renderer;app.frames->overlay=renderer.Render(scene);
        }
        if(wcsstr(command,L"--headless")){
            if(wcsstr(command,L"--demo-cards")){Scene scene;scene.cards={SampleCard(),SampleCard(true)};app.frames->cards->SetScene(scene);}
            if(wcsstr(command,L"--webcam")){
                if(app.devices.empty())throw HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                app.cameraIndex=1;app.frames->mode=2;
            }
            if(wcsstr(command,L"--publish")){auto camera=VirtualCamera();Check(camera->Start(nullptr));app.virtualCamera=camera;}
            unsigned seconds=15;auto arg=wcsstr(command,L"--seconds ");if(arg)seconds=std::clamp(static_cast<unsigned>(_wtoi(arg+10)),1u,300u);
            auto end=Clock::now()+std::chrono::seconds(seconds);
            while(Clock::now()<end){app.UpdateDemand();Sleep(33);}
        }else{
            WNDCLASSW wc{};wc.hInstance=instance;wc.lpszClassName=L"CallayersEditor";wc.lpfnWndProc=WindowProc;wc.style=CS_DBLCLKS;
            wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
            wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
            HWND window=CreateWindowW(wc.lpszClassName,L"Callayers",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1280,850,nullptr,nullptr,instance,&app);
            if(!window)throw HRESULT_FROM_WIN32(GetLastError());ShowWindow(window,show);
            if(wcsstr(command,L"--smoke-test"))SetTimer(window,2,1500,nullptr);
            if(wcsstr(command,L"--webcam")&&!app.devices.empty()){
                app.cameraIndex=1;app.shell->CameraIndex(1);app.Select();
            }
            if(wcsstr(command,L"--demo-cards")){app.editor->AddSampleCard();app.editor->AddSampleCard();}
            app.tray.Add(window);
            if(!app.shellOnly)app.StartOutput();
            MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){
                try{if(app.shell&&app.shell->Key(message))continue;}catch(...){EditorError(window);continue;}
                if(ContentPreTranslateMessage(&message))continue;
                TranslateMessage(&message);DispatchMessageW(&message);
            }
        }
    }catch(const winrt::hresult_error& error){result=error.code();StartupDiagnostic(error.message().c_str());if(!wcsstr(command,L"--headless")&&!wcsstr(command,L"--shell-only"))MessageBoxW(nullptr,error.message().c_str(),L"Callayers",MB_ICONERROR);}
    catch(HRESULT hr){result=static_cast<int>(hr);StartupDiagnostic(ErrorText(hr));if(!wcsstr(command,L"--headless")&&!wcsstr(command,L"--shell-only"))MessageBoxW(nullptr,ErrorText(hr).c_str(),L"Callayers",MB_ICONERROR);}
    catch(...){result=3;}
    if(xamlManager)xamlManager.Close();xamlManager=nullptr;xamlApplication=nullptr;
    if(dispatcher){auto shutdown=dispatcher.ShutdownQueueAsync();while(shutdown.Status()==winrt::Windows::Foundation::AsyncStatus::Started){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}dispatcher=nullptr;}
    if(bootstrapped)MddBootstrapShutdown();MFShutdown();CoUninitialize();return result;
}
