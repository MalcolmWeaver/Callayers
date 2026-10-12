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
    std::atomic<std::shared_ptr<const std::vector<BYTE>>> thumbnailPixels;
    bool thumbnails=false;
    Clock::time_point lastThumbnail{};
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
    uint64_t pendingEpoch=0;
    std::vector<std::function<void()>> commands;
    std::vector<CardStateChange> stateChanges;
    void Enqueue(std::function<void()> command){std::lock_guard lock(sceneMutex);commands.push_back(std::move(command));}
    void Commands(){std::vector<std::function<void()>> work;{std::lock_guard lock(sceneMutex);work.swap(commands);}for(auto& run:work)run();}
    void ApplyScene(){
        if(!ready)return;
        std::vector<CardElement> cards;uint64_t epoch;
        {std::lock_guard lock(sceneMutex);if(!changed)return;cards=pending;epoch=pendingEpoch;changed=false;}
        using namespace winrt::Windows::Data::Json;
        JsonArray data;
        for(const auto& c:cards){JsonObject o;o.SetNamedValue(L"epoch",JsonValue::CreateNumberValue(double(epoch)));o.SetNamedValue(L"state",JsonObject::Parse(c.state));o.SetNamedValue(L"source",JsonValue::CreateStringValue(c.html));
            o.SetNamedValue(L"scale",JsonValue::CreateNumberValue(c.renderScale));o.SetNamedValue(L"id",JsonValue::CreateStringValue(c.id));o.SetNamedValue(L"html",JsonValue::CreateStringValue(ResolveCardAssets(c.html,c.assets)));
            o.SetNamedValue(L"x",JsonValue::CreateNumberValue(c.x*Width));o.SetNamedValue(L"y",JsonValue::CreateNumberValue(c.y*Height));
            o.SetNamedValue(L"w",JsonValue::CreateNumberValue(c.width*Width));o.SetNamedValue(L"h",JsonValue::CreateNumberValue(c.height*Height));data.Append(o);}
        std::wstring script=L"window.setCards("+std::wstring(data.Stringify())+L")";
        Check(web->ExecuteScript(script.c_str(),nullptr));
    }
    Impl(bool preview):thumbnails(preview){thread=std::thread([this]{Run();});}
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
        try{
            if(thumbnails){
                if(Clock::now()-lastThumbnail>=std::chrono::milliseconds(250)){
                    auto pixels=std::make_shared<std::vector<BYTE>>(480*270*4);
                    for(UINT y=0;y<270;++y)for(UINT x=0;x<480;++x)memcpy(pixels->data()+(y*480+x)*4,static_cast<const BYTE*>(mapped.pData)+y*4*mapped.RowPitch+x*16,4);
                    thumbnailPixels=pixels;lastThumbnail=Clock::now();++count;
                }
            }else{latest=OverlayFromBgra(static_cast<const BYTE*>(mapped.pData),mapped.RowPitch);++count;}
        }
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
                    Check(settings->put_AreHostObjectsAllowed(FALSE));Check(settings->put_IsWebMessageEnabled(TRUE));
                    EventRegistrationToken token;
                    Check(web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](auto*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT{
                        LPWSTR raw=nullptr;if(FAILED(args->get_WebMessageAsJson(&raw)))return S_OK;
                        std::wstring message(raw);CoTaskMemFree(raw);if(message.size()>600000)return S_OK;
                        try{using namespace winrt::Windows::Data::Json;auto data=JsonObject::Parse(message);CardStateChange change;
                            change.id=data.GetNamedString(L"id");change.html=data.GetNamedString(L"html");change.state=data.GetNamedObject(L"state").Stringify();change.epoch=uint64_t(data.GetNamedNumber(L"epoch"));
                            if(change.state.size()>65536)return S_OK;
                            std::lock_guard lock(sceneMutex);if(change.epoch!=pendingEpoch)return S_OK;
                            for(auto& card:pending)if(card.id==change.id&&card.html==change.html){
                                if(card.state==change.state)break;card.state=change.state;
                                std::erase_if(stateChanges,[&](const auto& old){return old.id==change.id;});stateChanges.push_back(std::move(change));break;
                            }
                        }catch(...){}return S_OK;
                    }).Get(),&token));
                    Check(web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](auto*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT{return args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);}).Get(),&token));
                    Check(web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](auto*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT{return args->put_Handled(TRUE);}).Get(),&token));
                    Check(web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([this](auto*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT{
                        // Only the initial, application-owned NavigateToString is allowed.
                        if(!initialNavigation)return args->put_Cancel(TRUE);initialNavigation=false;return S_OK;
                    }).Get(),&token));
                    Check(web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([this](auto*,auto*)->HRESULT{error=E_FAIL;latest=nullptr;return S_OK;}).Get(),&token));
                    Check(web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this](auto*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT{BOOL ok=FALSE;args->get_IsSuccess(&ok);ready=ok!=FALSE;if(!ok)error=E_FAIL;return S_OK;}).Get(),&token));
                    Check(web->NavigateToString(LR"HTML(<!doctype html><meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data: blob:; font-src data:; frame-src about:; connect-src 'none'; base-uri 'none'; form-action 'none'"><style>html,body{margin:0;width:1920px;height:1080px;overflow:hidden;background:transparent}iframe{position:absolute;border:0;background:transparent}</style><script>
const cards=new Map();let requestNumber=0;const flushRequests=new Map();
function cardRuntime(initial){
 let state=Object.assign(Object.create(null),initial),restoring=false;
 const fields=()=>document.querySelectorAll('[data-state-key],input[id],textarea[id],select[id],[contenteditable][id],details[id]');
 const key=e=>e.getAttribute('data-state-key')||e.id;
 function read(e){if(e.matches('input[type=password],input[type=file]'))return undefined;if(e.matches('details'))return e.open;if(e.matches('input[type=checkbox],input[type=radio]'))return e.checked;if(e.matches('input,textarea,select'))return e.value;if(e.isContentEditable)return e.textContent;}
 function snapshot(){for(const e of fields()){const v=read(e),k=key(e);if(k&&v!==undefined)state[k]=v;}}
 function send(request){snapshot();if(JSON.stringify(state).length<=65536)parent.postMessage({type:'callayers-state',state,request},'*');}
 function restore(){restoring=true;for(const e of fields()){const k=key(e);if(!Object.hasOwn(state,k))continue;const v=state[k];if(e.matches('input[type=password],input[type=file]'))continue;if(e.matches('details'))e.open=!!v;else if(e.matches('input[type=checkbox],input[type=radio]'))e.checked=!!v;else if(e.matches('input,textarea,select'))e.value=String(v??'');else if(e.isContentEditable)e.textContent=String(v??'');}restoring=false;window.dispatchEvent(new CustomEvent('callayers-state',{detail:window.callayers.getState()}));}
 window.callayers=Object.freeze({getState:()=>JSON.parse(JSON.stringify(state)),setState:patch=>{if(!patch||typeof patch!=='object'||Array.isArray(patch))return;const next=Object.assign(Object.create(null),state,patch);if(JSON.stringify(next).length>65536)return;state=next;restore();send();}});
 addEventListener('message',event=>{if(event.source!==parent)return;const data=event.data;if(data?.type==='callayers-restore'){state=Object.assign(Object.create(null),data.state);restore();}if(data?.type==='callayers-flush')send(data.request);});
 for(const type of ['input','change','toggle'])document.addEventListener(type,()=>{if(!restoring)send();},true);
 document.addEventListener('submit',event=>event.preventDefault(),true);
 if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',restore,{once:true});else restore();
}
function documentFor(c){const state=JSON.stringify(c.state).replace(/</g,'\\u003c');return '<!doctype html><script>('+cardRuntime.toString()+')('+state+');<'+ '/script>'+c.html.replace(/<!doctype[^>]*>/i,'');}
function makeFrame(){const f=document.createElement('iframe');f.sandbox='allow-scripts';f.setAttribute('allow',"camera 'none'; microphone 'none'; geolocation 'none'; clipboard-read 'none'; clipboard-write 'none'");return f}
function position(f,c,z){if(f)Object.assign(f.style,{left:c.x+'px',top:c.y+'px',width:(c.w/(c.scale||1))+'px',height:(c.h/(c.scale||1))+'px',transformOrigin:'top left',transform:'scale('+(c.scale||1)+')',zIndex:z+''})}
addEventListener('message',event=>{
 if(event.data?.type!=='callayers-state')return;
 for(const [id,e] of cards){if(event.source!==e.frame?.contentWindow)continue;const state=event.data.state;if(!state||typeof state!=='object'||Array.isArray(state)||JSON.stringify(state).length>65536)return;e.state=JSON.stringify(state);
 window.chrome.webview.postMessage({id,html:e.source,epoch:e.epoch,state});
 const request=event.data.request;if(flushRequests.has(request)){flushRequests.get(request)();flushRequests.delete(request);}break;}
});
window.flushCards=async()=>{const waits=[];for(const e of cards.values()){if(!e.frame)continue;waits.push(new Promise(resolve=>{const request=++requestNumber;const timer=setTimeout(()=>{flushRequests.delete(request);resolve(false)},800);flushRequests.set(request,()=>{clearTimeout(timer);resolve(true)});e.frame.contentWindow.postMessage({type:'callayers-flush',request},'*');}));}return (await Promise.all(waits)).every(Boolean);};
window.setCards=list=>{
 const keep=new Set(list.map(c=>c.id));
 for(const [id,e] of cards)if(!keep.has(id)){e.frame?.remove();e.pending?.remove();cards.delete(id)}
 for(const [z,c] of list.entries()){
  let e=cards.get(c.id);
  if(e&&e.epoch!==c.epoch){e.frame?.remove();e.pending?.remove();cards.delete(c.id);e=null;}
  if(!e){e={frame:null,html:null,pending:null,desired:null,epoch:c.epoch,state:JSON.stringify(c.state)};cards.set(c.id,e)}
  if(e.html!==c.html&&e.desired!==c.html){
   e.pending?.remove();const f=makeFrame();e.pending=f;e.desired=c.html;f.style.opacity='0';f.style.pointerEvents='none';position(f,c,z);
   f.onload=()=>requestAnimationFrame(()=>requestAnimationFrame(()=>{
    if(cards.get(c.id)!==e||e.pending!==f)return;
    e.frame?.remove();e.frame=f;e.html=c.html;e.source=c.source;e.pending=null;e.desired=null;e.state=JSON.stringify(c.state);f.style.opacity='1';f.style.pointerEvents='auto';
   }));
   f.srcdoc=documentFor(c);document.body.append(f);
  }else if(e.html===c.html&&e.pending){e.pending.remove();e.pending=null;e.desired=null}
  const state=JSON.stringify(c.state);if(e.frame&&e.state!==state){e.state=state;e.frame.contentWindow.postMessage({type:'callayers-restore',state:c.state},'*');}
  position(e.frame,c,z);position(e.pending,c,z);
 }
};</script><body></body>)HTML"));

                }catch(const winrt::hresult_error& e){error=e.code();}catch(HRESULT e){error=e;}catch(...){error=E_FAIL;}return S_OK;
            }).Get());
        }).Get()));
    }
    void Run(){
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        try{Initialize();while(!stopping){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}ApplyScene();Commands();Poll();MsgWaitForMultipleObjects(0,nullptr,FALSE,16,QS_ALLINPUT);}}
        catch(const winrt::hresult_error& e){error=e.code();}catch(HRESULT hr){error=hr;}catch(...){error=E_FAIL;}
        stopping=true;
        if(session)session.Close();if(pool)pool.Close();session=nullptr;pool=nullptr;
        if(controller)controller->Close();web.Reset();controller.Reset();composition.Reset();
        visual=nullptr;compositor=nullptr;if(window)DestroyWindow(window);
        if(queue){auto done=queue.ShutdownQueueAsync();while(done.Status()==winrt::Windows::Foundation::AsyncStatus::Started){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(1);}queue=nullptr;}
        CoUninitialize();
    }
};
CardHost::CardHost(bool thumbnails):impl(std::make_unique<Impl>(thumbnails)){}
std::shared_ptr<const std::vector<BYTE>> CardHost::ThumbnailPixels()const{return impl->thumbnailPixels.load();}
CardHost::~CardHost()=default;
void CardHost::SetScene(const Scene& scene,uint64_t epoch){std::lock_guard lock(impl->sceneMutex);if(impl->pending==scene.cards&&impl->pendingEpoch==epoch)return;impl->pending=scene.cards;impl->pendingEpoch=epoch;impl->changed=true;}
std::vector<CardStateChange> CardHost::TakeStates(){std::lock_guard lock(impl->sceneMutex);std::vector<CardStateChange> changes;changes.swap(impl->stateChanges);return changes;}
void CardHost::Mouse(UINT message,POINT point,WPARAM buttons){
    impl->Enqueue([host=impl.get(),message,point,buttons]{if(!host->ready)return;
        auto kind=message==WM_LBUTTONDOWN?COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN:message==WM_LBUTTONUP?COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP:message==WM_LBUTTONDBLCLK?COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK:message==WM_MOUSEWHEEL?COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL:message==WM_MOUSELEAVE?COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE:COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;
        UINT data=message==WM_MOUSEWHEEL?static_cast<UINT>(static_cast<INT32>(GET_WHEEL_DELTA_WPARAM(buttons))):0;
        host->composition->SendMouseInput(kind,static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(LOWORD(buttons)),data,point);
    });
}
void CardHost::InsertText(const std::wstring& text){impl->Enqueue([host=impl.get(),text]{if(!host->ready)return;winrt::Windows::Data::Json::JsonObject args;args.SetNamedValue(L"text",winrt::Windows::Data::Json::JsonValue::CreateStringValue(text));host->web->CallDevToolsProtocolMethod(L"Input.insertText",args.Stringify().c_str(),nullptr);});}
void CardHost::Key(UINT message,WPARAM key,LPARAM flags){
    int modifiers=((GetKeyState(VK_MENU)&0x8000)?1:0)|((GetKeyState(VK_CONTROL)&0x8000)?2:0)|((GetKeyState(VK_SHIFT)&0x8000)?8:0);
    impl->Enqueue([host=impl.get(),message,key,flags,modifiers]{if(!host->ready)return;using namespace winrt::Windows::Data::Json;JsonObject args;
        args.SetNamedValue(L"type",JsonValue::CreateStringValue(message==WM_KEYUP?L"keyUp":L"rawKeyDown"));args.SetNamedValue(L"windowsVirtualKeyCode",JsonValue::CreateNumberValue(double(key)));args.SetNamedValue(L"modifiers",JsonValue::CreateNumberValue(modifiers));args.SetNamedValue(L"autoRepeat",JsonValue::CreateBooleanValue(message==WM_KEYDOWN&&(flags&(1LL<<30))));
        std::wstring name=key==VK_BACK?L"Backspace":key==VK_DELETE?L"Delete":key==VK_RETURN?L"Enter":key==VK_TAB?L"Tab":key==VK_LEFT?L"ArrowLeft":key==VK_RIGHT?L"ArrowRight":key==VK_UP?L"ArrowUp":key==VK_DOWN?L"ArrowDown":key==VK_HOME?L"Home":key==VK_END?L"End":key==VK_SPACE?L" ":L"";
        if(message==WM_KEYDOWN&&(modifiers&2)){
            const wchar_t* command=key=='A'?L"selectAll":key=='Z'?((modifiers&8)?L"redo":L"undo"):key=='Y'?L"redo":nullptr;
            if(command){JsonArray commands;commands.Append(JsonValue::CreateStringValue(command));args.SetNamedValue(L"commands",commands);}
        }
        if(!name.empty())args.SetNamedValue(L"key",JsonValue::CreateStringValue(name));
        host->web->CallDevToolsProtocolMethod(L"Input.dispatchKeyEvent",args.Stringify().c_str(),nullptr);
    });
}
bool CardHost::FlushState(){
    struct Completion {HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::atomic<bool> ok=false;~Completion(){if(event)CloseHandle(event);}};
    auto done=std::make_shared<Completion>();if(!done->event)return false;
    impl->Enqueue([host=impl.get(),done]{if(!host->ready){done->ok=true;SetEvent(done->event);return;}
        HRESULT hr=host->web->CallDevToolsProtocolMethod(L"Runtime.evaluate",LR"JSON({"expression":"window.flushCards()","awaitPromise":true,"returnByValue":true})JSON",Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>([done](HRESULT hr,LPCWSTR json)->HRESULT{
            try{if(SUCCEEDED(hr)&&json)done->ok=winrt::Windows::Data::Json::JsonObject::Parse(json).GetNamedObject(L"result").GetNamedBoolean(L"value",false);}catch(...){}SetEvent(done->event);return S_OK;
        }).Get());if(FAILED(hr))SetEvent(done->event);
    });
    return WaitForSingleObject(done->event,1500)==WAIT_OBJECT_0&&done->ok;
}
std::shared_ptr<const TextOverlay> CardHost::Latest()const{return impl->latest.load();}
HRESULT CardHost::Error()const{return impl->error;}
uint64_t CardHost::FrameCount()const{return impl->count;}
}
