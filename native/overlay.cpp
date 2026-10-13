#include "overlay.h"
#include "frame_bridge.h"
#include "renderer_log.h"
#include "resource_diagnostics.h"
#include <WebView2EnvironmentOptions.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <iterator>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;
namespace callayers {
static std::wstring Utf8(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring result(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), result.data(), n);
    return result;
}
Overlay::~Overlay() {
    close();
}
void Overlay::close() {
    *alive_ = false;
    initialDocumentPending_ = false; documentNavigationSeen_ = false;
    if (controller_) controller_->Close();
    view_.Reset(); controller_.Reset();
    if (host_) DestroyWindow(host_);
    host_ = nullptr;
}
void Overlay::fail(const char* stage, HRESULT result, int detail) {
    log_->record(stage, result, detail);
    state->ready = false; state->failed = true;
}
void Overlay::retry(HINSTANCE instance) {
    close();
    alive_ = std::make_shared<bool>(true);
    pending_ = false;
    state->ready = false; state->failed = false;
    ++state->currentEpoch;
    { std::lock_guard lock(state->mutex); state->png.clear(); state->tick = 0; }
    initialize(instance);
}
void Overlay::initialize(HINSTANCE instance) {
    if (!log_) log_ = std::make_shared<RendererLog>();
    log_->record("initialize");
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = instance; wc.lpszClassName = L"CallayersOverlayHost";
    RegisterClassW(&wc);
    // Independent, non-activating host: minimizing the editor must not minimize
    // the browser. Background throttling is disabled only for this environment.
    host_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"Callayers overlay", WS_POPUP,
        -16000, -16000, Width, Height, nullptr, nullptr, instance, nullptr);
    if (!host_) { fail("create-host-window", HRESULT_FROM_WIN32(GetLastError())); return; }
    ShowWindow(host_, SW_SHOWNOACTIVATE);
    wchar_t* local = nullptr;
    HRESULT folderResult = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
    if (FAILED(folderResult) || !local) { CoTaskMemFree(local); fail("locate-profile-folder", FAILED(folderResult) ? folderResult : E_FAIL); return; }
    std::filesystem::path folder = std::filesystem::path(local) / L"Callayers" / L"Foundation" / L"WebView2";
    CoTaskMemFree(local);
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(L"--disable-background-timer-throttling --disable-renderer-backgrounding --disable-backgrounding-occluded-windows --disable-features=CalculateNativeWinOcclusion");
    auto alive = alive_;
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, folder.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this, alive](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (!*alive) return S_OK;
            if (FAILED(result) || !env) { fail("environment-completed", FAILED(result) ? result : E_FAIL); return S_OK; }
            log_->record("environment-ready");
            configure(env); return S_OK;
        }).Get());
    if (FAILED(hr)) fail("create-environment", hr);
}
void Overlay::configure(ICoreWebView2Environment* environment) {
    auto alive = alive_;
    ComPtr<ICoreWebView2Environment> env = environment;
    HRESULT hr = environment->CreateCoreWebView2Controller(host_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [this, alive, env](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
        if (!*alive) return S_OK;
        if (FAILED(result) || !controller) { fail("controller-completed", FAILED(result) ? result : E_FAIL); return S_OK; }
        log_->record("controller-ready");
        controller_ = controller;
        HRESULT configured = controller_->get_CoreWebView2(&view_);
        if (FAILED(configured) || !view_) { fail("get-webview", FAILED(configured) ? configured : E_FAIL); return S_OK; }
        ComPtr<ICoreWebView2Controller3> scale;
        if (SUCCEEDED(controller_.As(&scale))) {
            scale->put_ShouldDetectMonitorScaleChanges(FALSE);
            scale->put_RasterizationScale(1.0);
            scale->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
        }
        RECT bounds{0, 0, Width, Height};
        if (FAILED(configured = controller_->put_Bounds(bounds))) { fail("set-bounds", configured); return S_OK; }
        ComPtr<ICoreWebView2Controller2> transparent;
        if (FAILED(configured = controller_.As(&transparent))) { fail("get-transparent-controller", configured); return S_OK; }
        if (FAILED(configured = transparent->put_DefaultBackgroundColor({0, 0, 0, 0}))) { fail("set-transparent-background", configured); return S_OK; }
        ComPtr<ICoreWebView2Settings> settings;
        if (FAILED(configured = view_->get_Settings(&settings)) || !settings) { fail("get-settings", FAILED(configured) ? configured : E_FAIL); return S_OK; }
        settings->put_AreDevToolsEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreHostObjectsAllowed(FALSE);
        settings->put_IsWebMessageEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        EventRegistrationToken token{};
        view_->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
            args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK;
        }).Get(), &token);
        view_->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            args->put_Handled(TRUE); return S_OK;
        }).Get(), &token);
        view_->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([this, alive](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            if (!*alive) return S_OK;
            // This newly created WebView has no card content yet. Admit the one
            // navigation requested below by NavigateToString, then lock main-frame
            // navigation. Its event URI is not necessarily the document's final
            // about:blank source; comparing those cancelled our own startup page.
            // The independent request blocker, CSP and iframe sandbox still apply.
            BOOL redirected = FALSE;
            UINT64 navigation = 0;
            HRESULT redirectResult = args->get_IsRedirected(&redirected);
            HRESULT idResult = args->get_NavigationId(&navigation);
            bool allowed = initialDocumentPending_ && SUCCEEDED(redirectResult) && !redirected && SUCCEEDED(idResult);
            if (allowed) {
                initialDocumentPending_ = false;
                documentNavigationSeen_ = true;
                documentNavigation_ = navigation;
            }
            log_->record(allowed ? "internal-navigation" : "blocked-navigation");
            if (!allowed) args->put_Cancel(TRUE);
            return S_OK;
        }).Get(), &token);
        view_->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            wchar_t* uri = nullptr; args->get_Uri(&uri);
            if (!uri || (wcscmp(uri, L"about:srcdoc") != 0 && wcscmp(uri, L"about:blank") != 0)) args->put_Cancel(TRUE);
            CoTaskMemFree(uri); return S_OK;
        }).Get(), &token);
        view_->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        view_->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([env](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
            ComPtr<ICoreWebView2WebResourceResponse> denied;
            env->CreateWebResourceResponse(nullptr, 403, L"Blocked", L"Content-Type: text/plain", &denied);
            args->put_Response(denied.Get()); return S_OK;
        }).Get(), &token);
        ComPtr<ICoreWebView2_4> view4;
        if (SUCCEEDED(view_.As(&view4))) view4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
            args->put_Cancel(TRUE); return S_OK;
        }).Get(), &token);
        view_->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([this, alive](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
            if (!*alive) return S_OK;
            COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
            args->get_ProcessFailedKind(&kind);
            // GPU/utility exits are auto-recovered by WebView2. Treating every
            // process event as fatal permanently disabled a usable renderer.
            if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED ||
                kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                kind == COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED)
                fail("process-failed", E_FAIL, int(kind));
            else log_->record("recoverable-process-event", S_OK, int(kind));
            return S_OK;
        }).Get(), &token);
        view_->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this, alive](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
            if (!*alive) return S_OK;
            UINT64 navigation = 0;
            HRESULT navigationResult = args->get_NavigationId(&navigation);
            if (FAILED(navigationResult)) { fail("read-navigation-id", navigationResult); return S_OK; }
            if (!documentNavigationSeen_ || navigation != documentNavigation_) {
                log_->record("ignored-navigation-completion");
                return S_OK;
            }
            BOOL success = FALSE; args->get_IsSuccess(&success);
            COREWEBVIEW2_WEB_ERROR_STATUS error{}; args->get_WebErrorStatus(&error);
            log_->record("navigation-completed", success ? S_OK : E_FAIL, int(error));
            if (!success) {
                // Cancelling a disallowed later navigation preserves the current
                // document; it must not take the working compositor offline.
                if (!(state->ready && error == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED))
                    fail("load-overlay-document", E_FAIL, int(error));
                return S_OK;
            }
            if (success) {
                view_->ExecuteScript(L"typeof window.setScene === 'function' && typeof window.stopScene === 'function'",
                    Callback<ICoreWebView2ExecuteScriptCompletedHandler>([this, alive](HRESULT result, LPCWSTR value) -> HRESULT {
                        if (!*alive) return S_OK;
                        if (FAILED(result) || !value || wcscmp(value, L"true") != 0) { fail("overlay-script-not-ready", FAILED(result) ? result : E_FAIL); return S_OK; }
                        state->ready = true; state->failed = false; log_->record("overlay-ready");
                        update(scene_);
                        if (!enabled_) { enabled_ = true; enable(false); }
                        return S_OK;
                    }).Get());
            }
            return S_OK;
        }).Get(), &token);
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::ifstream file(std::filesystem::path(exe).parent_path() / L"web" / L"overlay.html", std::ios::binary);
        std::string html((std::istreambuf_iterator<char>(file)), {});
        if (html.empty()) { fail("read-overlay-file", HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)); return S_OK; }
        log_->record("overlay-file-loaded", S_OK, int(html.size()));
        initialDocumentPending_ = true;
        if (FAILED(configured = view_->NavigateToString(Utf8(html).c_str()))) {
            initialDocumentPending_ = false;
            fail("navigate-to-overlay", configured);
        }
        return S_OK;
    }).Get());
    if (FAILED(hr)) fail("create-controller", hr);
}
void Overlay::update(const std::wstring& json) {
    scene_ = json;
    if (view_ && state->ready && enabled_ && !json.empty())
        view_->ExecuteScript((L"window.setScene(" + json + L")").c_str(), nullptr);
}
void Overlay::enable(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    ++state->currentEpoch;
    if (!view_ || !state->ready) return;
    ComPtr<ICoreWebView2_3> suspend;
    view_.As(&suspend);
    if (enabled) {
        if (suspend) suspend->Resume();
        controller_->put_IsVisible(TRUE);
        update(scene_);
    } else {
        controller_->put_IsVisible(FALSE);
        auto alive = alive_;
        view_->ExecuteScript(L"window.stopScene()", Callback<ICoreWebView2ExecuteScriptCompletedHandler>([this, alive, suspend](HRESULT, LPCWSTR) -> HRESULT {
            if (!*alive || enabled_ || !suspend) return S_OK;
            suspend->TrySuspend(Callback<ICoreWebView2TrySuspendCompletedHandler>([this, alive](HRESULT, BOOL) -> HRESULT {
                if (*alive && enabled_) {
                    ComPtr<ICoreWebView2_3> resume; if (SUCCEEDED(view_.As(&resume))) resume->Resume();
                }
                return S_OK;
            }).Get());
            return S_OK;
        }).Get());
    }
}
void Overlay::capture() {
    if (!view_ || !state->ready || state->failed || !enabled_ || pending_) return;
    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return;
    pending_ = true;
    auto alive = alive_; auto output = state;
    unsigned epoch = state->currentEpoch;
    ULONGLONG tick = GetTickCount64();
    auto captureStart = DiagnosticClock::now();
    HRESULT hr = view_->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(),
        Callback<ICoreWebView2CapturePreviewCompletedHandler>([this, alive, output, stream, epoch, tick, captureStart](HRESULT result) -> HRESULT {
            if (!*alive) return S_OK;
            pending_ = false;
            if (FAILED(result) || epoch != output->currentEpoch || !enabled_) return S_OK;
            RecordDuration(VideoStage::OverlayCapture, captureStart);
            HGLOBAL global = nullptr; STATSTG stat{};
            if (FAILED(GetHGlobalFromStream(stream.Get(), &global)) || FAILED(stream->Stat(&stat, STATFLAG_NONAME))) return S_OK;
            if (stat.cbSize.QuadPart > 32 * 1024 * 1024) return S_OK;
            auto data = static_cast<const uint8_t*>(GlobalLock(global));
            if (data) {
                std::lock_guard lock(output->mutex);
                output->png.assign(data, data + size_t(stat.cbSize.QuadPart)); output->tick = tick; output->epoch = epoch;
                GlobalUnlock(global);
            }
            return S_OK;
        }).Get());
    if (FAILED(hr)) pending_ = false;
}
std::shared_ptr<OverlayImage> DecodeOverlay(const std::vector<uint8_t>& png, ULONGLONG tick) {
    StageTimer timing(VideoStage::OverlayDecode);
    if (png.empty()) return {};
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return {};
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(png.data(), UINT(png.size())));
    if (!stream) return {};
    ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter))) return {};
    UINT width = 0, height = 0; frame->GetSize(&width, &height);
    if (width != Width || height != Height) return {};
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    auto output = std::make_shared<OverlayImage>(); output->pixels.resize(PixelBytes); output->tick = tick;
    if (FAILED(converter->CopyPixels(nullptr, Width * 4, UINT(PixelBytes), output->pixels.data()))) return {};
    return output;
}
}
