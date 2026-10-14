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
static unsigned long long GetSystemTimeAsFileTimeStamp(){FILETIME time{};GetSystemTimeAsFileTime(&time);return ((static_cast<unsigned long long>(time.dwHighDateTime)<<32)|time.dwLowDateTime)/10000;}
Overlay::~Overlay(){
#ifdef CALLAYERS_GENERATION
    generatedCheckpoint(true);
#endif
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
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
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
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
#ifdef CALLAYERS_GENERATION
            offlineFiles_=std::make_unique<OfflineFiles>(directory);platform.set_file_system(offlineFiles_.get());
#else
            platform.set_file_system(ultralight::GetPlatformFileSystem(directory.string().c_str()));
#endif
#ifdef CALLAYERS_GENERATION
            platform.set_logger(nullptr); // Generated scripts must not write contents to renderer logs.
#else
            platform.set_logger(ultralight::GetDefaultLogger((directory/"diagnostics/ultralight.log").string().c_str()));
#endif
            renderer_=ultralight::Renderer::Create();
        }
        ultralight::ViewConfig config;config.is_accelerated=false;config.is_transparent=true;
#ifdef CALLAYERS_GENERATION
        config.allow_universal_access_from_file_urls=false;
        config.clipboard_read_policy=ultralight::ClipboardReadPolicy::Deny;
        loadGenerated();documentsPending_=true;for(auto& card:generated_)card.restored=false;
#endif
        view_=renderer_->CreateView(Width,Height,config,nullptr);
        auto html=winrt::to_string(ReadTextDocument(L"overlay.html"));
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
        auto path=std::filesystem::path([]{static wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,32768);return file;}()).parent_path()/L"web"/L"interactive-card.js";
        std::ifstream input(path,std::ios::binary);std::string cardScript{std::istreambuf_iterator<char>(input),{}};
        if(!input || cardScript.empty())throw std::runtime_error("Missing interactive card fixture");
        auto marker=html.rfind("</script>");if(marker==std::string::npos)throw std::runtime_error("Missing overlay script");
        html.insert(marker,cardScript);
        const std::string noInput="pointer-events:none;transform-origin";
        html.replace(html.find(noInput),noInput.size(),"pointer-events:auto;transform-origin");
        restoreNeeded_=true;
#endif
#ifdef CALLAYERS_LIBRARY
        const std::string noInput="pointer-events:none;transform-origin";
        html.replace(html.find(noInput),noInput.size(),"pointer-events:auto;transform-origin");
#endif
        view_->LoadHTML(html.c_str());
        opened_=GetTickCount64();scenePending_=true;state->ready=false;state->failed=false;
        log_->record("ultralight-cpu-started");
    }catch(const winrt::hresult_error& error){fail(error.code());}catch(...){fail(E_FAIL);}
}
void Overlay::retry(HINSTANCE instance){
#ifdef CALLAYERS_GENERATION
    generatedCheckpoint(true);
#endif
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
    checkpoint(true);
    if(restoreBlocked_)storeLoaded_=false;
#endif
    view_=nullptr;
    {std::lock_guard lock(state->mutex);state->image.reset();state->privateImage.reset();state->privateTextId=0;}
    ++state->currentEpoch;initialize(instance);
}
void Overlay::update(const std::wstring& json){scene_=json;scenePending_=true;
#ifdef CALLAYERS_TELEMETRY
    ++telemetryScene_;telemetryPicture_.reset();
#endif
}
void Overlay::enable(bool enabled){
    if(enabled_==enabled)return;
    enabled_=enabled;++state->currentEpoch;scenePending_=true;
    if(!enabled){
#ifdef CALLAYERS_GENERATION
        generatedCheckpoint(true);for(auto& card:generated_)card.restored=false;
#endif
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
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
    surface->UnlockPixels();(*slot)->tick=tick;
#ifdef CALLAYERS_TELEMETRY
    if(!telemetryPicture_){auto picture=std::make_shared<TelemetryPicture>();picture->scene=telemetryScene_;for(const auto& c:generated_)if(c.visible)picture->cards.push_back({winrt::to_string(c.id),c.image.empty()?"ai":"image"});telemetryPicture_=std::move(picture);}
    (*slot)->telemetry=telemetryPicture_;
#endif
    return *slot;
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
#ifdef CALLAYERS_GENERATION
            generatedCheckpoint(false);installGenerated();
#endif
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
            auto scene=winrt::Windows::Data::Json::JsonObject::Parse(scene_);
            auto cards=scene.GetNamedArray(L"cards");
            const bool hiding=cards.Size() && !cards.GetObjectAt(0).GetNamedBoolean(L"visible");
            cardVisible_=!hiding;
            checkpoint(hiding);
            if(hiding)restoreNeeded_=true;
#endif
            if(!scene_.empty())script("window.setScene("+winrt::to_string(scene_)+")");scenePending_=false;
        }
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
        restoreCard();checkpoint(false);
        if(saveRevision_){auto result=cardStore_.result();if(result.revision==saveRevision_){state->saveError=result.error;saveRevision_=0;}}
#endif
#ifdef CALLAYERS_GENERATION
        restoreGenerated();generatedCheckpoint(false);
        if(generatedRevision_){auto result=generatedStore_.result();if(result.revision==generatedRevision_){state->generatedSaveError=result.error;
#ifdef CALLAYERS_TELEMETRY
            telemetrySaved(result.revision,result.error);
#endif
            generatedRevision_=0;}}
#endif
        const auto epoch=state->currentEpoch.load();const auto tick=GetTickCount64();
        renderer_->Render();
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
        if(cardVisible_ && restoreNeeded_)return; // Never publish defaults before saved values restore.
#endif
#ifdef CALLAYERS_GENERATION
        for(auto& card:generated_)if(card.visible && !card.restored)return;
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
#if defined(CALLAYERS_INTERACTIVE) && !defined(CALLAYERS_LIBRARY)
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
#endif
#ifdef CALLAYERS_INTERACTIVE
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
void Overlay::retryState(){
#ifndef CALLAYERS_LIBRARY
    if(restoreBlocked_){storeLoaded_=false;retry(GetModuleHandleW(nullptr));}else checkpoint(true);
#endif
#ifdef CALLAYERS_GENERATION
#ifdef CALLAYERS_LIBRARY
    if(replacement_){retryReplacement();return;}
#endif
    if(generatedBlocked_){generatedLoaded_=false;generatedBlocked_=false;loadGenerated();documentsPending_=true;scenePending_=true;}else generatedCheckpoint(true);
#endif
}
#endif
#ifdef CALLAYERS_GENERATION
bool Overlay::loadGenerated(){
    if(generatedLoaded_)return !generatedBlocked_;
    HRESULT error=S_OK;auto json=generatedStore_.load(error);
    if(FAILED(error)||!ParseGeneratedCards(json,generated_)){generatedBlocked_=true;state->generatedSaveError=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);}
#ifdef CALLAYERS_LIBRARY
    nextRenderId_=0;for(auto& c:generated_)c.renderId=nextRenderId_++;
#endif
    if(!generatedBlocked_)state->generatedSaveError=S_OK;
    generatedLoaded_=true;return !generatedBlocked_;
}
void Overlay::installGenerated(){
    if(!documentsPending_)return;
#ifdef CALLAYERS_LIBRARY
    winrt::Windows::Data::Json::JsonObject documents;for(auto& c:generated_)documents.SetNamedValue(winrt::to_hstring(std::to_string(c.renderId)),winrt::Windows::Data::Json::JsonValue::CreateStringValue(CardDocument(c)));
    script("documents.length=0;Object.assign(documents,"+winrt::to_string(documents.Stringify())+")");
#else
    winrt::Windows::Data::Json::JsonArray items;for(auto& c:generated_)items.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(CardDocument(c)));
    script("documents.splice(3,documents.length-3,..."+winrt::to_string(items.Stringify())+")");
#endif
    documentsPending_=false;
}
bool Overlay::addGenerated(GeneratedCard card){
    if(generatedBlocked_ || generated_.size()>=16 || !ValidGeneratedCard(card))return false;
#ifdef CALLAYERS_LIBRARY
    if(replacement_)return false;card.renderId=nextRenderId_++;card.layer=int(generated_.size());card.edited=GetSystemTimeAsFileTimeStamp();
#endif
    generated_.push_back(std::move(card));documentsPending_=true;scenePending_=true;
#ifdef CALLAYERS_TELEMETRY
    auto creationKind=generated_.back().image.empty()?"ai_create":"image_import";
#endif
    lastGeneratedJson_=GeneratedCardsJson(generated_);generatedRevision_=generatedStore_.save(lastGeneratedJson_);
#ifdef CALLAYERS_TELEMETRY
    pendingCreations_.push_back({generated_.back().id,creationKind,generatedRevision_});
#endif
    return true;
}
void Overlay::generatedLayout(int index,double x,double y,double w,double h,bool visible){
    if(index<0||index>=int(generated_.size()))return;auto& c=generated_[index];
    if(c.visible&&!visible){generatedCheckpoint(true);c.restored=false;}
    c.x=x;c.y=y;c.w=w;c.h=h;c.visible=visible;
}
void Overlay::restoreGenerated(){
    for(size_t i=0;i<generated_.size();++i){auto& c=generated_[i];if(!c.visible||c.restored)continue;
        auto frame="card-"+std::to_string(
#ifdef CALLAYERS_LIBRARY
c.renderId
#else
i+3
#endif
);ultralight::String error;
        auto ready=view_->EvaluateScript("document.readyState === 'complete'",&error,frame.c_str());
        if(!error.empty() || ready!=ultralight::String("true"))continue;
        auto code="if(window.CallayersCard && typeof window.CallayersCard.restore==='function')window.CallayersCard.restore("+winrt::to_string(c.state)+")";
        view_->EvaluateScript(code.c_str(),&error,frame.c_str());if(!error.empty()){state->generatedSaveError=E_FAIL;continue;}c.restored=true;c.lastRunning=false;
    }
}
void Overlay::generatedCheckpoint(bool force){
#ifdef CALLAYERS_LIBRARY
    if(replacement_)return;
#endif
    if(generatedBlocked_)return;auto now=GetTickCount64();if(!force && now-lastGeneratedPoll_<250)return;lastGeneratedPoll_=now;
    bool changed=false;
    if(view_ && state->ready){for(size_t i=0;i<generated_.size();++i){auto& c=generated_[i];if(!c.visible||!c.restored)continue;
        ultralight::String error;auto frame="card-"+std::to_string(
#ifdef CALLAYERS_LIBRARY
c.renderId
#else
i+3
#endif
);
        auto result=view_->EvaluateScript("window.CallayersCard && typeof window.CallayersCard.snapshot==='function' ? JSON.stringify(window.CallayersCard.snapshot()) : '{}'",&error,frame.c_str());
        if(!error.empty()||result.empty())continue;
        auto bytes=result.utf8();auto json=std::wstring(winrt::to_hstring(std::string(bytes.data(),bytes.length())));
        if(!ValidCardState(json)){state->generatedSaveError=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);continue;}
        auto running=view_->EvaluateScript("!!(window.CallayersCard && window.CallayersCard.isRunning && window.CallayersCard.isRunning())",&error,frame.c_str())==ultralight::String("true");
        if(json!=c.state && (force||!running||running!=c.lastRunning||now-c.lastSave>=5000)){c.state=json;c.lastSave=now;changed=true;}c.lastRunning=running;
    }}
    if(force || changed){auto json=GeneratedCardsJson(generated_);if(json!=lastGeneratedJson_ || FAILED(state->generatedSaveError.load())){lastGeneratedJson_=json;generatedRevision_=generatedStore_.save(json);}}
}
#ifdef CALLAYERS_LIBRARY
bool Overlay::renameCard(int index,const std::wstring& name){
    if(generatedBlocked_ || replacement_ || index<0 || index>=int(generated_.size()))return false;
    auto renamed=generated_[index];renamed.name=name;if(!ValidGeneratedCard(renamed))return false;
    if(renamed.name==generated_[index].name)return true;
    ++renamed.contentVersion;renamed.edited=GetSystemTimeAsFileTimeStamp();generated_[index]=std::move(renamed);generatedCheckpoint(true);return true;
}
bool Overlay::deleteCard(int index){
    if(generatedBlocked_ || replacement_ || index<0 || index>=int(generated_.size()))return false;
    generatedCheckpoint(true);int layer=generated_[index].layer;generated_.erase(generated_.begin()+index);for(auto& c:generated_)if(c.layer>layer)--c.layer;
    documentsPending_=scenePending_=true;generatedCheckpoint(true);return true;
}
bool Overlay::stackCard(int index,bool front){
    if(generatedBlocked_ || replacement_ || index<0 || index>=int(generated_.size()))return false;
    auto& target=generated_[index];int old=target.layer,next=front?int(generated_.size())-1:0;
    for(auto& c:generated_)if(&c!=&target){if(front && c.layer>old)--c.layer;if(!front && c.layer<old)++c.layer;}
    target.layer=next;scenePending_=true;generatedCheckpoint(true);return true;
}
bool Overlay::queueReplacement(int index,GeneratedCard card){
    if(generatedBlocked_ || replacement_ || index<0 || index>=int(generated_.size()) || !ValidGeneratedCard(card))return false;
    generatedCheckpoint(true);card.edited=GetSystemTimeAsFileTimeStamp();auto saved=generated_;saved[index]=card;
    auto revision=generatedStore_.save(GeneratedCardsJson(saved));replacement_=Replacement{generated_[index].id,generated_[index].contentVersion,std::move(card),revision};
#ifdef CALLAYERS_TELEMETRY
    replacement_->telemetryGeneration=telemetryGeneration_;
#endif
    return true;
}
bool Overlay::reviseCard(const std::wstring& id,unsigned long long version,const GeneratedCard& result){
    for(size_t i=0;i<generated_.size();++i)if(MatchesRevisionTarget(generated_[i],id,version)){GeneratedCard card;if(!RevisedCard(generated_[i],result,card))return false;bool queued=queueReplacement(int(i),std::move(card));if(queued)replacement_->revised=true;return queued;}return false;
}
bool Overlay::restorePrevious(int index){if(index<0 || index>=int(generated_.size()))return false;GeneratedCard card;if(!RestorePrevious(generated_[index],card))return false;return queueReplacement(index,std::move(card));}
int Overlay::pollReplacement(){
    if(!replacement_ || replacement_->failed)return 0;auto result=generatedStore_.result();if(result.revision!=replacement_->saveRevision)return 0;
    state->generatedSaveError=result.error;if(FAILED(result.error)){replacement_->failed=true;return -1;}
    for(auto& current:generated_)if(MatchesRevisionTarget(current,replacement_->id,replacement_->version)){
        auto card=std::move(replacement_->card);card.x=current.x;card.y=current.y;card.w=current.w;card.h=card.w*card.height/card.width;double fit=std::min({1.0,(1280-card.x)/card.w,(720-card.y)/card.h});card.w*=fit;card.h*=fit;card.visible=current.visible;card.layer=current.layer;card.renderId=nextRenderId_++;current=std::move(card);
#ifdef CALLAYERS_TELEMETRY
        if(replacement_->revised && replacement_->telemetryGeneration==telemetryGeneration_ && creation_)creation_("ai_revision",winrt::to_string(current.id));
#endif
        replacement_.reset();documentsPending_=scenePending_=true;generatedCheckpoint(true);return 1;
    }
    replacement_.reset();generatedCheckpoint(true);return -1;
}
void Overlay::retryReplacement(){if(replacement_ && replacement_->failed){auto saved=generated_;for(auto& c:saved)if(c.id==replacement_->id)c=replacement_->card;replacement_->saveRevision=generatedStore_.save(GeneratedCardsJson(saved));replacement_->failed=false;}}
#endif
#ifdef CALLAYERS_TELEMETRY
void Overlay::telemetrySaved(unsigned revision,HRESULT error){
    if(FAILED(error))return;
    for(auto it=pendingCreations_.begin();it!=pendingCreations_.end();){if(it->revision>revision){++it;continue;}
        if(std::any_of(generated_.begin(),generated_.end(),[&](const auto& c){return c.id==it->id;}) && creation_)creation_(it->kind,winrt::to_string(it->id));
        it=pendingCreations_.erase(it);
    }
}
#endif
#endif
}
