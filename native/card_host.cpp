#include "card_host.h"
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <DispatcherQueue.h>
#include <d3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <mutex>
#include <thread>
#include <shlobj.h>
#include <wrl.h>

namespace callayers {
using Microsoft::WRL::Make;
using Microsoft::WRL::Callback;
namespace comp=winrt::Windows::UI::Composition;
namespace cap=winrt::Windows::Graphics::Capture;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
struct CardHost::Impl {
    std::atomic<bool> stopping=false;
    std::atomic<HRESULT> error=S_OK;
    std::atomic<uint64_t> count=0;
    std::atomic<std::shared_ptr<const TextOverlay>> latest;
    std::thread thread;
    HWND window=nullptr;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2CompositionController> composition;
    ComPtr<ICoreWebView2> web;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> staging;
    comp::Compositor compositor{nullptr};
    comp::ContainerVisual visual{nullptr};
    cap::Direct3D11CaptureFramePool pool{nullptr};
    cap::GraphicsCaptureSession session{nullptr};
    winrt::Windows::System::DispatcherQueueController queue{nullptr};
    bool ready=false,initialNavigation=true;
    std::mutex sceneMutex;
    std::vector<CardElement> pending;
    bool changed=true;
    void ApplyScene(){
        if(!ready)return;
        std::vector<CardElement> cards;
        {std::lock_guard lock(sceneMutex);if(!changed)return;cards=pending;changed=false;}
        using namespace winrt::Windows::Data::Json;
        JsonArray data;
        for(const auto& c:cards){JsonObject o;
            o.SetNamedValue(L"id",JsonValue::CreateStringValue(c.id));o.SetNamedValue(L"html",JsonValue::CreateStringValue(ResolveCardAssets(c.html,c.assets)));
            o.SetNamedValue(L"x",JsonValue::CreateNumberValue(c.x*Width));o.SetNamedValue(L"y",JsonValue::CreateNumberValue(c.y*Height));
            o.SetNamedValue(L"w",JsonValue::CreateNumberValue(c.width*Width));o.SetNamedValue(L"h",JsonValue::CreateNumberValue(c.height*Height));data.Append(o);}
        std::wstring script=L"window.setCards("+std::wstring(data.Stringify())+L")";
        Check(web->ExecuteScript(script.c_str(),nullptr));
    }
    Impl(){thread=std::thread([this]{Run();});}
    ~Impl(){stopping=true;if(thread.joinable())thread.join();}
    void StartCapture(){
        auto item=cap::GraphicsCaptureItem::CreateFromVisual(visual);
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        ComPtr<IDXGIDevice> dxgi;Check(device.As(&dxgi));
        winrt::com_ptr<IInspectable> inspectable;
        Check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(),inspectable.put()));
        auto direct=inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
        pool=cap::Direct3D11CaptureFramePool::CreateFreeThreaded(direct,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,{Width,Height});
        session=pool.CreateCaptureSession(item);session.IsCursorCaptureEnabled(false);session.StartCapture();
    }
    void Poll(){
        if(!pool)return;auto frame=pool.TryGetNextFrame();if(!frame)return;
        auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;Check(access->GetInterface(IID_PPV_ARGS(&texture)));
        if(!staging){D3D11_TEXTURE2D_DESC desc;texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(device->CreateTexture2D(&desc,nullptr,&staging));}
        context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        try{latest=OverlayFromBgra(static_cast<const BYTE*>(mapped.pData),mapped.RowPitch);++count;}
        catch(...){context->Unmap(staging.Get(),0);throw;}
        context->Unmap(staging.Get(),0);
    }
    void Initialize(){
        DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
        Check(CreateDispatcherQueueController(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(queue))));
        compositor=comp::Compositor();visual=compositor.CreateContainerVisual();visual.Size({Width,Height});
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Callayers card renderer",WS_POPUP,0,0,Width,Height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw HRESULT_FROM_WIN32(GetLastError());
        auto optionsWeb=Make<CoreWebView2EnvironmentOptions>();
        Check(optionsWeb->put_AdditionalBrowserArguments(L"--disable-background-timer-throttling --disable-renderer-backgrounding --disable-backgrounding-occluded-windows"));
        wchar_t local[MAX_PATH];Check(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,local));
        auto folder=std::filesystem::path(local)/L"Callayers"/L"WebView2";
        Check(CreateCoreWebView2EnvironmentWithOptions(nullptr,folder.c_str(),optionsWeb.Get(),Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this](HRESULT hr,ICoreWebView2Environment* env)->HRESULT{
            if(stopping)return S_OK;if(FAILED(hr)){error=hr;return S_OK;}
            ComPtr<ICoreWebView2Environment3> env3;hr=env->QueryInterface(IID_PPV_ARGS(&env3));if(FAILED(hr)){error=hr;return S_OK;}
            return env3->CreateCoreWebView2CompositionController(window,Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>([this](HRESULT result,ICoreWebView2CompositionController* created)->HRESULT{
                if(stopping)return S_OK;
                if(FAILED(result)){error=result;return S_OK;}
                try{
                    composition=created;Check(composition.As(&controller));Check(controller->get_CoreWebView2(&web));
                    ComPtr<ICoreWebView2Controller3> scale;Check(controller.As(&scale));
                    Check(scale->put_ShouldDetectMonitorScaleChanges(FALSE));Check(scale->put_RasterizationScale(1.0));
                    Check(scale->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS));
                    Check(controller->put_Bounds({0,0,Width,Height}));Check(controller->put_IsVisible(TRUE));
                    ComPtr<ICoreWebView2Controller2> transparent;Check(controller.As(&transparent));Check(transparent->put_DefaultBackgroundColor({0,0,0,0}));
                    Check(composition->put_RootVisualTarget(winrt::get_unknown(visual)));
                    StartCapture();
                    ComPtr<ICoreWebView2Settings> settings;Check(web->get_Settings(&settings));
                    Check(settings->put_AreDevToolsEnabled(FALSE));Check(settings->put_AreDefaultContextMenusEnabled(FALSE));
                    Check(settings->put_AreHostObjectsAllowed(FALSE));Check(settings->put_IsWebMessageEnabled(FALSE));
                    EventRegistrationToken token;
                    Check(web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](auto*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT{return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);}).Get(),&token));
                    Check(web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](auto*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{return args->put_Handled(TRUE);}).Get(),&token));
                    Check(web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([this](auto*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
                        // Only the initial, application-owned NavigateToString is allowed.
                        if(!initialNavigation)return args->put_Cancel(TRUE);initialNavigation=false;return S_OK;
                    }).Get(),&token));
                    Check(web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([this](auto*,auto*)->HRESULT{error=E_FAIL;latest=nullptr;return S_OK;}).Get(),&token));
                    Check(web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this](auto*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT{BOOL ok=FALSE;args->get_IsSuccess(&ok);ready=ok!=FALSE;if(!ok)error=E_FAIL;return S_OK;}).Get(),&token));
                    Check(web->NavigateToString(LR"HTML(<!doctype html><meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data: blob:; font-src data:; frame-src about:; connect-src 'none'; base-uri 'none'; form-action 'none'"><style>html,body{margin:0;width:1920px;height:1080px;overflow:hidden;background:transparent}iframe{position:absolute;border:0;background:transparent}</style><script>
const cards=new Map();
function makeFrame(){const f=document.createElement('iframe');f.sandbox='allow-scripts';f.setAttribute('allow',"camera 'none'; microphone 'none'; geolocation 'none'; clipboard-read 'none'; clipboard-write 'none'");return f}
function position(f,c,z){if(f)Object.assign(f.style,{left:c.x+'px',top:c.y+'px',width:c.w+'px',height:c.h+'px',zIndex:z+''})}
window.setCards=list=>{
 const keep=new Set(list.map(c=>c.id));
 for(const [id,e] of cards)if(!keep.has(id)){e.frame?.remove();e.pending?.remove();cards.delete(id)}
 for(const [z,c] of list.entries()){
  let e=cards.get(c.id);
  if(!e){e={frame:null,html:null,pending:null,desired:null};cards.set(c.id,e)}
  if(e.html!==c.html&&e.desired!==c.html){
   e.pending?.remove();const f=makeFrame();e.pending=f;e.desired=c.html;f.style.opacity='0';position(f,c,z);
   f.onload=()=>requestAnimationFrame(()=>requestAnimationFrame(()=>{
    if(cards.get(c.id)!==e||e.pending!==f)return;
    e.frame?.remove();e.frame=f;e.html=c.html;e.pending=null;e.desired=null;f.style.opacity='1';
   }));
   f.srcdoc=c.html;document.body.append(f);
  }else if(e.html===c.html&&e.pending){e.pending.remove();e.pending=null;e.desired=null}
  position(e.frame,c,z);position(e.pending,c,z);
 }
};</script><body></body>)HTML"));

                }catch(const winrt::hresult_error& e){error=e.code();}catch(HRESULT e){error=e;}catch(...){error=E_FAIL;}return S_OK;
            }).Get());
        }).Get()));
    }
    void Run(){
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        try{Initialize();while(!stopping){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}ApplyScene();Poll();MsgWaitForMultipleObjects(0,nullptr,FALSE,16,QS_ALLINPUT);}}
        catch(const winrt::hresult_error& e){error=e.code();}catch(HRESULT hr){error=hr;}catch(...){error=E_FAIL;}
        stopping=true;
        if(session)session.Close();if(pool)pool.Close();session=nullptr;pool=nullptr;
        if(controller)controller->Close();web.Reset();controller.Reset();composition.Reset();
        visual=nullptr;compositor=nullptr;if(window)DestroyWindow(window);
        if(queue){auto done=queue.ShutdownQueueAsync();while(done.Status()==winrt::Windows::Foundation::AsyncStatus::Started){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(1);}queue=nullptr;}
        CoUninitialize();
    }
};
CardHost::CardHost():impl(std::make_unique<Impl>()){}
CardHost::~CardHost()=default;
void CardHost::SetScene(const Scene& scene){std::lock_guard lock(impl->sceneMutex);if(impl->pending==scene.cards)return;impl->pending=scene.cards;impl->changed=true;}
std::shared_ptr<const TextOverlay> CardHost::Latest()const{return impl->latest.load();}
HRESULT CardHost::Error()const{return impl->error;}
uint64_t CardHost::FrameCount()const{return impl->count;}
}
