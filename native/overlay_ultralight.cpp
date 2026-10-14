#include "overlay_ultralight.h"
#include "frame_bridge.h"
#include "renderer_log.h"
#include "resource_diagnostics.h"
#include "text_style.h"
#include <AppCore/Platform.h>
#include <algorithm>
#include <cstring>

namespace callayers {
Overlay::~Overlay(){view_=nullptr;renderer_=nullptr;}
void Overlay::fail(HRESULT error){state->failed=true;state->ready=false;if(log_)log_->record("ultralight-overlay-failed",error);}
void Overlay::script(const std::string& code){
    ultralight::String error;view_->EvaluateScript(code.c_str(),&error);
    if(!error.empty())throw std::runtime_error("Overlay script failed");
}
void Overlay::initialize(HINSTANCE instance){
    (void)instance;
    if(!log_)log_=std::make_shared<RendererLog>();
    try{
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
        view_->LoadHTML(winrt::to_string(ReadTextDocument(L"overlay.html")).c_str());
        opened_=GetTickCount64();scenePending_=true;state->ready=false;state->failed=false;
        log_->record("ultralight-cpu-started");
    }catch(const winrt::hresult_error& error){fail(error.code());}catch(...){fail(E_FAIL);}
}
void Overlay::retry(HINSTANCE instance){
    view_=nullptr;
    {std::lock_guard lock(state->mutex);state->image.reset();state->privateImage.reset();state->privateTextId=0;}
    ++state->currentEpoch;initialize(instance);
}
void Overlay::update(const std::wstring& json){scene_=json;scenePending_=true;}
void Overlay::enable(bool enabled){
    if(enabled_==enabled)return;
    enabled_=enabled;++state->currentEpoch;scenePending_=true;
    if(!enabled){
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
        if(scenePending_){if(!scene_.empty())script("window.setScene("+winrt::to_string(scene_)+")");scenePending_=false;}
        const auto epoch=state->currentEpoch.load();const auto tick=GetTickCount64();
        renderer_->Render();auto output=copy(frames_,tick);if(!output)return;
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
}
