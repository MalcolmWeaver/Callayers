#include "text_editor.h"
#include "text_style.h"
#include <WebView2EnvironmentOptions.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace callayers {
using Microsoft::WRL::Callback;
using namespace winrt::Windows::Data::Json;
TextEditor::~TextEditor(){close();}
void TextEditor::close() {
    *alive_=false;ready_=false;
    if(controller_)controller_->Close();
    view_.Reset();controller_.Reset();
    if(host_)DestroyWindow(host_);host_=nullptr;finish_={};
}
void TextEditor::resize() {
    if(!host_)return;
    int width=std::max(1L,area_.right-area_.left);
    int height=std::clamp(int(std::ceil(std::max(minimumHeight_,height_)*scale_)),1,int(std::max(1L,area_.bottom-area_.top)));
    SetWindowPos(host_,HWND_TOP,area_.left,area_.top,width,height,SWP_NOACTIVATE);
    if(controller_){RECT bounds{0,0,width,height};controller_->put_Bounds(bounds);}
}
void TextEditor::focus(){if(controller_)controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);}
void TextEditor::failed(){if(*alive_ && !unavailable_){unavailable_=true;if(finish_)finish_(false);}}
void TextEditor::open(HWND parent,const RECT& area,const std::wstring& text,double font,const std::wstring& color,double width,double scale,double minimumHeight,std::function<void(bool)> finish) {
    close();parent_=parent;area_=area;text_=text;scale_=scale;height_=minimumHeight;minimumHeight_=minimumHeight;unavailable_=false;finish_=std::move(finish);alive_=std::make_shared<bool>(true);
    JsonObject options;options.SetNamedValue(L"text",JsonValue::CreateStringValue(text));options.SetNamedValue(L"color",JsonValue::CreateStringValue(color));
    options.SetNamedValue(L"font",JsonValue::CreateNumberValue(font));options.SetNamedValue(L"width",JsonValue::CreateNumberValue(width));options.SetNamedValue(L"scale",JsonValue::CreateNumberValue(scale));options_=options.Stringify();
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CallayersPrivateTextEditor";RegisterClassW(&wc);
    host_=CreateWindowExW(0,wc.lpszClassName,L"Annotation text",WS_CHILD|WS_CLIPSIBLINGS,0,0,1,1,parent_,nullptr,wc.hInstance,nullptr);
    if(!host_){failed();return;}resize();
    PWSTR local=nullptr;HRESULT hr=SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local);
    if(FAILED(hr) || !local){CoTaskMemFree(local);failed();return;}
    auto profile=std::filesystem::path(local)/L"Callayers"/L"Foundation"/L"WebView2";CoTaskMemFree(local);
    auto settings=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    settings->put_AdditionalBrowserArguments(L"--disable-background-timer-throttling --disable-renderer-backgrounding --disable-backgrounding-occluded-windows --disable-features=CalculateNativeWinOcclusion");
    auto alive=alive_;
    hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),settings.Get(),Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this,alive](HRESULT result,ICoreWebView2Environment* environment)->HRESULT {
        if(!*alive)return S_OK;if(FAILED(result) || !environment){failed();return S_OK;}configure(environment);return S_OK;
    }).Get());
    if(FAILED(hr))failed();
}
void TextEditor::configure(ICoreWebView2Environment* environment) {
    auto alive=alive_;
    HRESULT hr=environment->CreateCoreWebView2Controller(host_,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this,alive](HRESULT result,ICoreWebView2Controller* controller)->HRESULT {
        if(!*alive)return S_OK;if(FAILED(result)||!controller){failed();return S_OK;}
        controller_=controller;if(FAILED(controller_->get_CoreWebView2(&view_)) || !view_){failed();return S_OK;}
        Microsoft::WRL::ComPtr<ICoreWebView2Controller3> scale;
        if(SUCCEEDED(controller_.As(&scale))){scale->put_ShouldDetectMonitorScaleChanges(FALSE);scale->put_RasterizationScale(1.0);scale->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);}
        controller_->put_ZoomFactor(1.0);resize();
        Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;view_->get_Settings(&settings);
        if(!settings){failed();return S_OK;}
        settings->put_AreDevToolsEnabled(FALSE);settings->put_AreHostObjectsAllowed(FALSE);settings->put_IsWebMessageEnabled(TRUE);settings->put_IsZoomControlEnabled(FALSE);settings->put_IsStatusBarEnabled(FALSE);settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        EventRegistrationToken token{};
        controller_->add_LostFocus(Callback<ICoreWebView2FocusChangedEventHandler>([this,alive](ICoreWebView2Controller*,IUnknown*)->HRESULT {if(*alive && ready_ && finish_)finish_(true);return S_OK;}).Get(),&token);
        view_->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this,alive](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* args)->HRESULT {
            if(!*alive)return S_OK;PWSTR json=nullptr;
            if(SUCCEEDED(args->get_WebMessageAsJson(&json)) && json)try {
                auto message=JsonObject::Parse(json);auto type=message.GetNamedString(L"type");
                if(type==L"draft"){
                    auto text=message.GetNamedString(L"text");double height=message.GetNamedNumber(L"height");
                    if(text.size()<=2000 && std::isfinite(height) && height>0 && height<2000000){text_=text;height_=height;resize();}
                }else if(type==L"finish" && finish_)finish_(message.GetNamedBoolean(L"commit"));
            }catch(...){}
            CoTaskMemFree(json);return S_OK;
        }).Get(),&token);
        view_->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT {args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);return S_OK;}).Get(),&token);
        view_->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT {args->put_Handled(TRUE);return S_OK;}).Get(),&token);
        view_->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([this,alive](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* args)->HRESULT {
            if(!*alive)return S_OK;COREWEBVIEW2_PROCESS_FAILED_KIND kind{};args->get_ProcessFailedKind(&kind);
            if(kind==COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED || kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED || kind==COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED)failed();return S_OK;
        }).Get(),&token);
        auto initialNavigation=std::make_shared<bool>(true);
        view_->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([initialNavigation](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT {BOOL redirected=TRUE;args->get_IsRedirected(&redirected);if(!*initialNavigation || redirected)args->put_Cancel(TRUE);*initialNavigation=false;return S_OK;}).Get(),&token);
        view_->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this,alive](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT {
            if(!*alive)return S_OK;BOOL success=FALSE;args->get_IsSuccess(&success);if(!success){failed();return S_OK;}
            auto script=L"window.beginTextEdit("+options_+L")";
            HRESULT executed=view_->ExecuteScript(script.c_str(),Callback<ICoreWebView2ExecuteScriptCompletedHandler>([this,alive](HRESULT result,LPCWSTR)->HRESULT {
                if(!*alive)return S_OK;if(FAILED(result)){failed();return S_OK;}ShowWindow(host_,SW_SHOWNOACTIVATE);controller_->put_IsVisible(TRUE);ready_=true;focus();return S_OK;
            }).Get());if(FAILED(executed))failed();return S_OK;
        }).Get(),&token);
        try{auto html=ReadTextDocument(L"text-editor.html");if(FAILED(view_->NavigateToString(html.c_str())))failed();}catch(...){failed();}
        return S_OK;
    }).Get());if(FAILED(hr))failed();
}
}
