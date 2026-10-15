#include <windows.h>
#include <Ultralight/Ultralight.h>
#include <AppCore/Platform.h>
#include "offline_files.h"
#include "text_style.h"
#include "frame_bridge.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <iostream>
#include <thread>
int main(int argc,char** argv){try{
    if(argc!=2)return 2;winrt::init_apartment();wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto base=std::filesystem::path(exe).parent_path();
    callayers::OfflineFiles files(base);if(files.FileExists("../service-url.txt")||files.FileExists("web/overlay.html"))return 3;
    auto& platform=ultralight::Platform::instance();ultralight::Config config;config.memory_profile=ultralight::MemoryProfile::LowMemory;platform.set_config(config);
    platform.set_font_loader(ultralight::GetPlatformFontLoader());platform.set_file_system(&files);platform.set_logger(nullptr);
    auto renderer=ultralight::Renderer::Create();ultralight::ViewConfig options;options.is_transparent=true;options.allow_universal_access_from_file_urls=false;options.clipboard_read_policy=ultralight::ClipboardReadPolicy::Deny;
    options.initial_device_scale=callayers::OutputScale;
    auto view=renderer->CreateView(callayers::Width,callayers::Height,options,nullptr);view->LoadHTML(winrt::to_string(callayers::ReadTextDocument(L"overlay.html")).c_str());
    auto eval=[&](const std::string& source,const char* frame=""){ultralight::String error;auto result=view->EvaluateScript(source.c_str(),&error,frame);if(!error.empty())throw std::runtime_error("probe script error");return result;};
    bool ready=false;for(int i=0;i<100;++i){renderer->Update();renderer->Render();if(eval("typeof window.setScene")==ultralight::String("function")){ready=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}if(!ready)return 5;
    auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    require(eval("innerWidth===1280 && innerHeight===720 && devicePixelRatio===1.5")==ultralight::String("true"),"1080p changed the logical canvas");
    eval(R"JS(documents[0]=`<style>html,body{background:#ff0000}.box{display:none}</style><script>window.clicked=false;document.addEventListener('click',()=>window.clicked=true)</script>`;
      window.setScene({cards:[{id:0,visible:true,x:100,y:100,w:200,h:100,contentW:200,contentH:100}],annotations:[{kind:2,color:'#00ff00',size:0,x:1200,y:660,w:60,h:40}]});frames.get(0).style.pointerEvents='auto')JS");
    for(int i=0;i<30;++i){renderer->Update();renderer->Render();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    auto surface=view->surface();require(surface && surface->width()==1920 && surface->height()==1080,"Missing full 1080p surface");
    auto pixels=static_cast<const uint8_t*>(surface->LockPixels());require(pixels!=nullptr,"Missing renderer pixels");
    auto pixel=[&](int x,int y){return pixels+size_t(y)*surface->row_bytes()+size_t(x)*4;};
    bool placed=pixel(160,160)[2]==255 && pixel(440,290)[2]==255 && pixel(140,160)[3]==0 && pixel(460,290)[3]==0;
    bool edge=pixel(1845,1020)[1]==255;surface->UnlockPixels();
    require(placed,"Card size or placement changed at 1080p");require(edge,"Annotation missing near the canvas edge");
    view->Focus();view->FireMouseEvent({ultralight::MouseEvent::kType_MouseMoved,150,150,ultralight::MouseEvent::kButton_None});
    view->FireMouseEvent({ultralight::MouseEvent::kType_MouseDown,150,150,ultralight::MouseEvent::kButton_Left});
    view->FireMouseEvent({ultralight::MouseEvent::kType_MouseUp,150,150,ultralight::MouseEvent::kButton_Left});
    renderer->Update();require(eval("window.clicked","card-0")==ultralight::String("true"),"Card input no longer matches logical coordinates");
    eval("window.stopScene()");
    const std::string url=argv[1];
    std::string html="<script>window.probe={ran:true,storage:false,parent:false};try{localStorage.setItem('x','synthetic')}catch(e){probe.storage=true}try{parent.document.body.innerHTML='changed'}catch(e){probe.parent=true}fetch('"+url+"/fetch').catch(()=>{});try{let x=new XMLHttpRequest();x.open('GET','"+url+"/xhr',false);x.send()}catch(e){};let i=new Image();i.src='"+url+"/image';let f=document.createElement('iframe');f.src='"+url+"/frame';document.body.append(f);<"+"/script>";
    winrt::Windows::Data::Json::JsonArray docs;docs.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(html)));
    eval("documents.splice(3,0,..."+winrt::to_string(docs.Stringify())+");window.setScene({cards:[{id:3,visible:true,x:0,y:0,w:480,h:300,contentW:480,contentH:300}],annotations:[]})");
    for(int i=0;i<150;++i){renderer->Update();renderer->Render();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    auto result=eval("JSON.stringify(window.probe)","card-3");auto bytes=result.utf8();auto json=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(std::string(bytes.data(),bytes.length())));
    if(!json.GetNamedBoolean(L"ran")||!json.GetNamedBoolean(L"storage")||!json.GetNamedBoolean(L"parent"))return 4;
    std::cout<<"1080p surface, card placement, annotation edge and logical input passed\nOpaque frame, storage denial and packaged-only file access passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Renderer probe failed: "<<error.what()<<'\n';return 1;}catch(...){std::cerr<<"Renderer security probe failed\n";return 1;}}
