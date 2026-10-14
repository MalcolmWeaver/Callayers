#include "overlay_ultralight.h"
#include "frame_bridge.h"
#include "renderer_log.h"
#include "resource_diagnostics.h"
#include "text_style.h"
#include <AppCore/Platform.h>
#include <algorithm>
#include <cstring>
#ifdef CALLAYERS_INTERACTIVE
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#endif

namespace callayers {
Overlay::~Overlay(){
#ifdef CALLAYERS_INTERACTIVE
    checkpoint(true);
#endif
    view_=nullptr;renderer_=nullptr;
}
void Overlay::fail(HRESULT error){state->failed=true;state->ready=false;if(log_)log_->record("ultralight-overlay-failed",error);}
void Overlay::script(const std::string& code){
    ultralight::String error;view_->EvaluateScript(code.c_str(),&error);
    if(!error.empty())throw std::runtime_error("Overlay script failed");
}
void Overlay::initialize(HINSTANCE instance){
    (void)instance;
    if(!log_)log_=std::make_shared<RendererLog>();
    try{
#ifdef CALLAYERS_INTERACTIVE
        if(!storeLoaded_){
            HRESULT error=S_OK;auto json=cardStore_.load(error);
            try{auto items=winrt::Windows::Data::Json::JsonArray::Parse(json);
                if(items.Size()>1 || (items.Size()==1 && !ParseInteractiveState(std::wstring(items.GetAt(0).Stringify()),savedCard_)))error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }catch(...){error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);}
            restoreBlocked_=FAILED(error);state->saveError=error;storeLoaded_=true;
        }
#endif
        if(!renderer_){
            wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
            auto directory=std::filesystem::path(exe).parent_path();
            std::filesystem::create_directories(directory/L"diagnostics");
            auto& platform=ultralight::Platform::instance();
            ultralight::Config config;config.memory_profile=ultralight::MemoryProfile::LowMemory;
            config.resource_path_prefix="resources/";platform.set_config(config);
            platform.set_font_loader(ultralight::GetPlatformFontLoader());
            platform.set_file_system(ultralight::GetPlatformFileSystem(directory.string().c_str()));
            platform.set_logger(ultralight::GetDefaultLogger((directory/"diagnostics/ultralight.log").string().c_str()));
            renderer_=ultralight::Renderer::Create();
        }
        ultralight::ViewConfig config;config.is_accelerated=false;config.is_transparent=true;
        view_=renderer_->CreateView(Width,Height,config,nullptr);
        auto html=winrt::to_string(ReadTextDocument(L"overlay.html"));
#ifdef CALLAYERS_INTERACTIVE
        auto path=std::filesystem::path([]{static wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,32768);return file;}()).parent_path()/L"web"/L"interactive-card.js";
        std::ifstream input(path,std::ios::binary);std::string cardScript{std::istreambuf_iterator<char>(input),{}};
        if(!input || cardScript.empty())throw std::runtime_error("Missing interactive card fixture");
        auto marker=html.rfind("</script>");if(marker==std::string::npos)throw std::runtime_error("Missing overlay script");
        html.insert(marker,cardScript);
        const std::string noInput="pointer-events:none;transform-origin";
        html.replace(html.find(noInput),noInput.size(),"pointer-events:auto;transform-origin");
        restoreNeeded_=true;
#endif
        view_->LoadHTML(html.c_str());
        opened_=GetTickCount64();scenePending_=true;state->ready=false;state->failed=false;
        log_->record("ultralight-cpu-started");
    }catch(const winrt::hresult_error& error){fail(error.code());}catch(...){fail(E_FAIL);}
}
void Overlay::retry(HINSTANCE instance){
#ifdef CALLAYERS_INTERACTIVE
    checkpoint(true);
    if(restoreBlocked_)storeLoaded_=false;
#endif
    view_=nullptr;
    {std::lock_guard lock(state->mutex);state->image.reset();state->privateImage.reset();state->privateTextId=0;}
    ++state->currentEpoch;initialize(instance);
}
void Overlay::update(const std::wstring& json){scene_=json;scenePending_=true;}
void Overlay::enable(bool enabled){
    if(enabled_==enabled)return;
    enabled_=enabled;++state->currentEpoch;scenePending_=true;
    if(!enabled){
#ifdef CALLAYERS_INTERACTIVE
        checkpoint(true);blur();restoreNeeded_=true;
#endif
        try{if(view_ && state->ready)script("window.stopScene()");}catch(...){fail(E_FAIL);}
        std::lock_guard lock(state->mutex);state->image.reset();state->privateImage.reset();state->privateTextId=0;
        for(auto& image:frames_)if(image && image.use_count()==1)image.reset();
        for(auto& image:privateFrames_)if(image && image.use_count()==1)image.reset();
    }
}
std::shared_ptr<OverlayImage> Overlay::copy(std::array<std::shared_ptr<OverlayImage>,3>& pool,ULONGLONG tick){
    auto slot=std::find_if(pool.begin(),pool.end(),[](const auto& frame){return !frame || frame.use_count()==1;});
    if(slot==pool.end())return {};
    if(!*slot)*slot=std::make_shared<OverlayImage>();
    (*slot)->pixels.resize(PixelBytes);
    auto surface=view_->surface();if(!surface)return {};
    auto pixels=static_cast<const uint8_t*>(surface->LockPixels());if(!pixels)return {};
    for(int y=0;y<Height;++y)memcpy((*slot)->pixels.data()+size_t(y)*Width*4,pixels+size_t(y)*surface->row_bytes(),Width*4);
    surface->UnlockPixels();(*slot)->tick=tick;return *slot;
}
void Overlay::capture(){
    if(!view_ || !enabled_ || state->failed)return;
    StageTimer timing(VideoStage::OverlayCapture);
    try{
        renderer_->Update();
        if(!state->ready){
            if(view_->is_loading()){if(GetTickCount64()-opened_>15000)fail(HRESULT_FROM_WIN32(ERROR_TIMEOUT));return;}
            auto ready=view_->EvaluateScript("typeof window.setScene === 'function'");
            if(ready!=ultralight::String("true")){if(GetTickCount64()-opened_>15000)fail(E_FAIL);return;}
            state->ready=true;
        }
        if(scenePending_){
#ifdef CALLAYERS_INTERACTIVE
            auto scene=winrt::Windows::Data::Json::JsonObject::Parse(scene_);
            auto cards=scene.GetNamedArray(L"cards");
            const bool hiding=cards.Size() && !cards.GetObjectAt(0).GetNamedBoolean(L"visible");
            cardVisible_=!hiding;
            checkpoint(hiding);
            if(hiding)restoreNeeded_=true;
#endif
            if(!scene_.empty())script("window.setScene("+winrt::to_string(scene_)+")");scenePending_=false;
        }
#ifdef CALLAYERS_INTERACTIVE
        restoreCard();checkpoint(false);
        if(saveRevision_){auto result=cardStore_.result();if(result.revision==saveRevision_){state->saveError=result.error;saveRevision_=0;}}
#endif
        const auto epoch=state->currentEpoch.load();const auto tick=GetTickCount64();
        renderer_->Render();
#ifdef CALLAYERS_INTERACTIVE
        if(cardVisible_ && restoreNeeded_)return; // Never publish defaults before saved values restore.
#endif
        auto output=copy(frames_,tick);if(!output)return;
        auto id=state->requestedPrivateTextId.load();std::shared_ptr<OverlayImage> privateOutput;
        if(id){
            script("for(const t of document.querySelectorAll('#ink [data-text-id]'))t.style.visibility=Number(t.dataset.textId)==="+std::to_string(id)+"?'hidden':'visible'");
            renderer_->Render();privateOutput=copy(privateFrames_,tick);
            script("for(const t of document.querySelectorAll('#ink [data-text-id]'))t.style.visibility='visible'");
            renderer_->Render();
            // Never expose an unclean editor backdrop if its pool is busy.
            if(!privateOutput)return;
        }else{
            for(auto& image:privateFrames_)if(image && image.use_count()==1)image.reset();
        }
        std::lock_guard lock(state->mutex);
        state->image=std::move(output);state->privateImage=std::move(privateOutput);
        state->privateTextId=id;state->epoch=epoch;
    }catch(const winrt::hresult_error& error){fail(error.code());}catch(...){fail(E_FAIL);}
}
#ifdef CALLAYERS_INTERACTIVE
void Overlay::restoreCard(){
    if(!restoreNeeded_ || !view_ || !cardVisible_)return;
    ultralight::String error;
    auto scriptText="typeof window.CallayersCard === 'object'";
    auto ready=view_->EvaluateScript(scriptText,&error,"card-0");
    if(!error.empty() || ready!=ultralight::String("true"))return;
    auto code="window.CallayersCard.restore("+winrt::to_string(InteractiveStateJson(savedCard_))+")";
    view_->EvaluateScript(code.c_str(),&error,"card-0");
    if(!error.empty())throw std::runtime_error("Card restore failed");
    restoreNeeded_=false;lastRunning_=false;
}
void Overlay::checkpoint(bool force){
    if(!view_ || restoreNeeded_ || restoreBlocked_)return;
    auto now=GetTickCount64();if(!force && now-lastStatePoll_<250)return;lastStatePoll_=now;
    try{ultralight::String error;
        auto result=view_->EvaluateScript("window.CallayersCard ? JSON.stringify(window.CallayersCard.snapshot()) : ''",&error,"card-0");
        if(!error.empty() || result.empty())return;
        auto bytes=result.utf8();InteractiveState value;
        if(!ParseInteractiveState(std::wstring(winrt::to_hstring(std::string(bytes.data(),bytes.length()))),value)){state->saveError=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);return;}
        auto timer=view_->EvaluateScript("window.CallayersCard.isRunning()",&error,"card-0");
        if(!error.empty() || (timer!=ultralight::String("true") && timer!=ultralight::String("false"))){state->saveError=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);return;}
        bool running=timer==ultralight::String("true");
        bool meaningful=value.count!=savedCard_.count || value.title!=savedCard_.title || running!=lastRunning_;
        lastRunning_=running;
        savedCard_=value;
        if(force || meaningful || now-lastStateSave_>=5000){
            auto json=L"["+InteractiveStateJson(savedCard_)+L"]";
            if(json!=lastSavedCardJson_ || FAILED(state->saveError.load())){saveRevision_=cardStore_.save(json);lastSavedCardJson_=json;}
            lastStateSave_=now;
        }
    }catch(...){state->saveError=E_FAIL;}
}
void Overlay::mouse(int type,int x,int y){
    if(!view_ || !enabled_ || !state->ready)return;
    if(type==1)view_->Focus();view_->FireMouseEvent({static_cast<ultralight::MouseEvent::Type>(type),x,y,type==0?ultralight::MouseEvent::kButton_None:ultralight::MouseEvent::kButton_Left});
}
void Overlay::key(UINT message,WPARAM wParam,LPARAM lParam){
    if(!view_ || !enabled_ || !state->ready)return;
    auto type=message==WM_CHAR?ultralight::KeyEvent::kType_Char:message==WM_KEYUP?ultralight::KeyEvent::kType_KeyUp:ultralight::KeyEvent::kType_RawKeyDown;
    view_->FireKeyEvent(ultralight::KeyEvent(type,wParam,lParam,false));
}
void Overlay::scroll(int x,int y,int delta){if(view_ && enabled_ && state->ready)view_->FireScrollEvent({ultralight::ScrollEvent::kType_ScrollByPixel,0,delta/3,x,y});}
void Overlay::blur(){if(view_){ultralight::String error;view_->EvaluateScript("window.CallayersCard && window.CallayersCard.blur()",&error,"card-0");view_->Unfocus();}}
void Overlay::pointerTarget(int id){if(view_ && state->ready)script("for(const [id,f] of frames)f.style.pointerEvents=("+std::to_string(id)+"<0 || id==="+std::to_string(id)+")?'auto':'none'");}
#endif
}
