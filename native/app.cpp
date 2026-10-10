#include "common.h"
#include "text_editor_ui.h"
#include <commctrl.h>
#include <fstream>

using namespace callayers;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::Make;
namespace {
struct Frames {
    std::atomic<std::shared_ptr<Frame>> latest{Pattern(0,true)};
    std::atomic<std::shared_ptr<Frame>> composed{Pattern(0,true)};
    std::atomic<std::shared_ptr<const TextOverlay>> overlay;
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
                if(frames->mode==1)frames->latest=Pattern(++frames->sequence);
                auto background=frames->latest.load();
                if(GetTickCount64()-background->header.capturedMs>750)background=frames->idle;
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
    HWND window=nullptr,choice=nullptr,status=nullptr,start=nullptr;
    std::shared_ptr<Frames> frames=std::make_shared<Frames>();
    std::unique_ptr<Publisher> publisher;
    std::unique_ptr<TextEditorUI> editor;
    ComPtr<Capture> capture;
    ComPtr<IMFVirtualCamera> virtualCamera;
    std::thread activation;
    std::atomic<bool> activating=false;
    std::atomic<HRESULT> activationResult=S_FALSE;
    std::vector<Device> devices;
    std::vector<BYTE> preview;
    PreviewBuffer paintBuffer;
    std::wstring displayedStatus;
    std::wstring cameraStatus=L"Camera output not started. Choose Test pattern or a webcam.";
    RECT stage{};
    ~App(){if(capture)capture->Close();if(activation.joinable())activation.join();publisher.reset();}
    void StartOutput(){
        if(activating)return;
        if(activation.joinable())activation.join();
        activating=true;activationResult=S_FALSE;cameraStatus=L"Starting virtual camera…";
        activation=std::thread([this]{
            CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            try{auto v=VirtualCamera();Check(v->Start(nullptr));virtualCamera=v;activationResult=S_OK;}
            catch(HRESULT hr){activationResult=hr;}catch(...){activationResult=E_FAIL;}
            activating=false;CoUninitialize();
        });
    }
    void Select(){
        if(capture){capture->Close();capture.Reset();}
        frames->error=S_OK;frames->latest=Pattern(0,true);
        int index=static_cast<int>(SendMessageW(choice,CB_GETCURSEL,0,0));
        if(index<=0){frames->mode=1;return;}
        frames->mode=2;capture=Make<Capture>();
        HRESULT hr=capture->Open(devices.at(index-1),frames);
        if(FAILED(hr))frames->error=hr;
    }
    void Layout(){
        RECT r;GetClientRect(window,&r);const int margin=20;
        MoveWindow(choice,margin,20,300,240,TRUE);MoveWindow(start,340,20,180,30,TRUE);
        MoveWindow(status,margin,100,std::max(100L,r.right-40),55,TRUE);
        const int availableW=std::max(1L,r.right-2*margin),availableH=std::max(1L,r.bottom-180);
        const double scale=std::min(double(availableW)/Width,double(availableH)/Height);
        int w=static_cast<int>(Width*scale),h=static_cast<int>(Height*scale);
        stage={(r.right-w)/2,165,(r.right+w)/2,165+h};
        if(editor)editor->SetStage(stage);
        InvalidateRect(window,nullptr,FALSE);
    }
    void Tick(){
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
        message+=L"\r\nText: click to place · Double-click to edit · Ctrl+Enter to finish · Corners scale · Side handles wrap · Ctrl+S saves";
        if(message!=displayedStatus){SetWindowTextW(status,message.c_str());displayedStatus=message;}
        const BOOL canStart=!activating;
        if(IsWindowEnabled(start)!=canStart)EnableWindow(start,canStart);
        if(!IsIconic(window)){
            if(editor&&editor->IsEditing()){
                auto background=frames->latest.load();
                if(GetTickCount64()-background->header.capturedMs>750)background=frames->idle;
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
    if(app->editor){LRESULT result;if(app->editor->Message(msg,wp,lp,result))return result;}
    switch(msg){
    case WM_CREATE:{
        app->choice=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,window,reinterpret_cast<HMENU>(101),nullptr,nullptr);
        SendMessageW(app->choice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Test pattern"));
        for(const auto& d:app->devices)SendMessageW(app->choice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(d.name.c_str()));
        SendMessageW(app->choice,CB_SETCURSEL,0,0);
        app->start=CreateWindowW(L"BUTTON",L"Start camera output",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,window,reinterpret_cast<HMENU>(102),nullptr,nullptr);
        app->status=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,0,0,0,0,window,nullptr,nullptr,nullptr);
        for(HWND control:{app->choice,app->start,app->status})SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
        app->editor=std::make_unique<TextEditorUI>(window,[app](auto overlay){app->frames->overlay=std::move(overlay);});
        SetTimer(window,1,33,nullptr);return 0;}
    case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={960,540};return 0;}
    case WM_SIZE:app->Layout();return 0;
    case WM_TIMER:app->Tick();return 0;
    case WM_COMMAND:
        if(LOWORD(wp)==101&&HIWORD(wp)==CBN_SELCHANGE)app->Select();
        if(LOWORD(wp)==102)app->StartOutput();return 0;
    case WM_ERASEBKGND:return 1; // WM_PAINT presents the complete buffered background.
    case WM_PAINT:{
        PAINTSTRUCT ps;HDC target=BeginPaint(window,&ps);
        RECT client;GetClientRect(window,&client);
        HDC dc=app->paintBuffer.Prepare(target,client.right,client.bottom);
        if(dc){
            FillRect(dc,&client,GetSysColorBrush(COLOR_WINDOW));
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
    case WM_CLOSE:if(app->editor&&!app->editor->MayDiscard())return 0;DestroyWindow(window);return 0;
    case WM_DESTROY:KillTimer(window,1);PostQuitMessage(0);return 0;
    }
    }catch(...){EditorError(window);return 0;}
    return DefWindowProcW(window,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show){
    HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 1;
    HRESULT startup=MFStartup(MF_VERSION);if(FAILED(startup)){CoUninitialize();return 2;}
    int result=0;
    try {
        App app;app.publisher=std::make_unique<Publisher>(app.frames);app.devices=Cameras();
        if(wcsstr(command,L"--demo-text")){
            Scene scene;TextElement text;text.id=NewId();text.text=L"Callayers text test";text.x=.05;text.y=.12;text.size=72;scene.texts.push_back(text);
            TextRenderer renderer;app.frames->overlay=renderer.Render(scene);
        }
        if(wcsstr(command,L"--headless")){
            if(wcsstr(command,L"--webcam")){
                if(app.devices.empty())throw HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                app.frames->mode=2;app.capture=Make<Capture>();Check(app.capture->Open(app.devices.front(),app.frames));
            }
            if(wcsstr(command,L"--publish")){auto camera=VirtualCamera();Check(camera->Start(nullptr));app.virtualCamera=camera;}
            unsigned seconds=15;auto arg=wcsstr(command,L"--seconds ");if(arg)seconds=std::clamp(static_cast<unsigned>(_wtoi(arg+10)),1u,300u);
            Sleep(seconds*1000);
        }else{
            WNDCLASSW wc{};wc.hInstance=instance;wc.lpszClassName=L"CallayersEditor";wc.lpfnWndProc=WindowProc;wc.style=CS_DBLCLKS;
            wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
            HWND window=CreateWindowW(wc.lpszClassName,L"Callayers — Camera path",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1100,760,nullptr,nullptr,instance,&app);
            if(!window)throw HRESULT_FROM_WIN32(GetLastError());ShowWindow(window,show);
            if(wcsstr(command,L"--webcam")&&!app.devices.empty()){
                SendMessageW(app.choice,CB_SETCURSEL,1,0);app.Select();
            }
            if(wcsstr(command,L"--publish"))app.StartOutput();
            MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){
                try{if(app.editor&&app.editor->Key(message))continue;}catch(...){EditorError(window);continue;}
                if(IsDialogMessageW(window,&message))continue;
                TranslateMessage(&message);DispatchMessageW(&message);
            }
        }
    }catch(HRESULT hr){result=static_cast<int>(hr);if(!wcsstr(command,L"--headless"))MessageBoxW(nullptr,ErrorText(hr).c_str(),L"Callayers",MB_ICONERROR);}
    catch(...){result=3;}
    MFShutdown();CoUninitialize();return result;
}
