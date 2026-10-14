#include <windows.h>
#include <Ultralight/Ultralight.h>
#include <AppCore/Platform.h>
#include "offline_files.h"
#include "text_style.h"
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
    auto view=renderer->CreateView(1280,720,options,nullptr);view->LoadHTML(winrt::to_string(callayers::ReadTextDocument(L"overlay.html")).c_str());
    auto eval=[&](const std::string& source,const char* frame=""){ultralight::String error;auto result=view->EvaluateScript(source.c_str(),&error,frame);if(!error.empty())throw std::runtime_error("probe script error");return result;};
    bool ready=false;for(int i=0;i<100;++i){renderer->Update();renderer->Render();if(eval("typeof window.setScene")==ultralight::String("function")){ready=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}if(!ready)return 5;
    const std::string url=argv[1];
    std::string html="<script>window.probe={ran:true,storage:false,parent:false};try{localStorage.setItem('x','synthetic')}catch(e){probe.storage=true}try{parent.document.body.innerHTML='changed'}catch(e){probe.parent=true}fetch('"+url+"/fetch').catch(()=>{});try{let x=new XMLHttpRequest();x.open('GET','"+url+"/xhr',false);x.send()}catch(e){};let i=new Image();i.src='"+url+"/image';let f=document.createElement('iframe');f.src='"+url+"/frame';document.body.append(f);<"+"/script>";
    winrt::Windows::Data::Json::JsonArray docs;docs.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring(html)));
    eval("documents.splice(3,0,..."+winrt::to_string(docs.Stringify())+");window.setScene({cards:[{id:3,visible:true,x:0,y:0,w:480,h:300,contentW:480,contentH:300}],annotations:[]})");
    for(int i=0;i<150;++i){renderer->Update();renderer->Render();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    auto result=eval("JSON.stringify(window.probe)","card-3");auto bytes=result.utf8();auto json=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(std::string(bytes.data(),bytes.length())));
    if(!json.GetNamedBoolean(L"ran")||!json.GetNamedBoolean(L"storage")||!json.GetNamedBoolean(L"parent"))return 4;
    std::cout<<"Opaque frame, storage denial and packaged-only file access passed\n";return 0;
}catch(...){std::cerr<<"Renderer security probe failed\n";return 1;}}
