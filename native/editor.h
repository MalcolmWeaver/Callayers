#pragma once
#include "common.h"
#include <filesystem>
#include <cmath>
#include <set>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace callayers {
struct TextElement {
    std::wstring id,text=L"Type your text",family=L"Segoe UI";
    double x=.1,y=.15,width=.5,height=.2,size=56;
    uint32_t color=0xffffff;
    bool bold=false;
    int alignment=0;
    bool operator==(const TextElement&)const=default;
};
struct Scene {
    std::wstring title=L"Untitled";
    std::vector<TextElement> texts;
    bool operator==(const Scene&)const=default;
};
inline std::wstring NewId(){GUID id;Check(CoCreateGuid(&id));wchar_t text[40];StringFromGUID2(id,text,40);return text;}
class Document {
    std::vector<Scene> past,future;
    Scene saved;
public:
    Scene scene;
    bool Dirty()const{return scene!=saved;}
    bool CanUndo()const{return !past.empty();}
    bool CanRedo()const{return !future.empty();}
    void Commit(const Scene& before){
        if(scene==before)return;
        past.push_back(before);if(past.size()>100)past.erase(past.begin());future.clear();
    }
    void Undo(){if(past.empty())return;future.push_back(scene);scene=past.back();past.pop_back();}
    void Redo(){if(future.empty())return;past.push_back(scene);scene=future.back();future.pop_back();}
    void Loaded(Scene next){scene=std::move(next);saved=scene;past.clear();future.clear();}
    void Saved(){saved=scene;}
};
// Dragging uses a fixed starting element, so every move is absolute rather than cumulative.
inline TextElement MoveText(const TextElement& start,double dx,double dy){
    auto t=start;t.x=std::clamp(start.x+dx,0.,1.-t.width);t.y=std::clamp(start.y+dy,0.,1.-t.height);return t;
}
// Handles: 0 TL, 1 TR, 2 BR, 3 BL, 4 left side, 5 right side.
inline TextElement ResizeText(const TextElement& start,int handle,double dx,double dy){
    auto t=start;
    bool left=handle==0||handle==3||handle==4,top=handle==0||handle==1;
    if(handle>=4){
        double edge=left?std::clamp(start.x+dx,0.,start.x+start.width-.05):std::clamp(start.x+start.width+dx,start.x+.05,1.);
        t.x=left?edge:start.x;t.width=left?start.x+start.width-edge:edge-start.x;return t;
    }
    double sx=(start.width+(left?-dx:dx))/start.width;
    double sy=(start.height+(top?-dy:dy))/start.height;
    double scale=std::abs(sx-1)>std::abs(sy-1)?sx:sy;
    double maxX=(left?start.x+start.width:1.-start.x)/start.width;
    double maxY=(top?start.y+start.height:1.-start.y)/start.height;
    double lower=std::max({.05/start.width,.01/start.height,8./start.size});
    double upper=std::min({maxX,maxY,300./start.size});
    scale=std::clamp(scale,std::min(lower,upper),upper);
    t.width=start.width*scale;t.height=start.height*scale;t.size=start.size*scale;
    if(left)t.x=start.x+start.width-t.width;if(top)t.y=start.y+start.height-t.height;
    return t;
}
inline std::string EncodeScene(const Scene& scene){
    using namespace winrt::Windows::Data::Json;
    JsonObject root;root.SetNamedValue(L"format",JsonValue::CreateStringValue(L"callayers"));
    root.SetNamedValue(L"version",JsonValue::CreateNumberValue(1));root.SetNamedValue(L"title",JsonValue::CreateStringValue(scene.title));
    JsonArray elements;
    for(const auto& t:scene.texts){
        JsonObject o;
        o.SetNamedValue(L"type",JsonValue::CreateStringValue(L"text"));
        o.SetNamedValue(L"id",JsonValue::CreateStringValue(t.id));o.SetNamedValue(L"text",JsonValue::CreateStringValue(t.text));
        o.SetNamedValue(L"family",JsonValue::CreateStringValue(t.family));
        o.SetNamedValue(L"x",JsonValue::CreateNumberValue(t.x));o.SetNamedValue(L"y",JsonValue::CreateNumberValue(t.y));
        o.SetNamedValue(L"width",JsonValue::CreateNumberValue(t.width));o.SetNamedValue(L"height",JsonValue::CreateNumberValue(t.height));
        o.SetNamedValue(L"size",JsonValue::CreateNumberValue(t.size));o.SetNamedValue(L"color",JsonValue::CreateNumberValue(t.color));
        o.SetNamedValue(L"bold",JsonValue::CreateBooleanValue(t.bold));o.SetNamedValue(L"alignment",JsonValue::CreateNumberValue(t.alignment));
        elements.Append(o);
    }
    JsonObject shot;shot.SetNamedValue(L"elements",elements);JsonArray scenes;scenes.Append(shot);root.SetNamedValue(L"scenes",scenes);
    return winrt::to_string(root.Stringify());
}
inline Scene DecodeScene(const std::string& bytes){
    using namespace winrt::Windows::Data::Json;
    if(bytes.size()>8*1024*1024)throw std::runtime_error("This deck is too large to open.");
    auto root=JsonObject::Parse(winrt::to_hstring(bytes));
    if(root.GetNamedString(L"format")!=L"callayers"||root.GetNamedNumber(L"version")!=1)
        throw std::runtime_error("Unsupported Callayers file version.");
    auto scenes=root.GetNamedArray(L"scenes");
    if(scenes.Size()!=1)throw std::runtime_error("This editor currently supports one scene per file.");
    Scene scene;scene.title=root.GetNamedString(L"title");
    auto items=scenes.GetObjectAt(0).GetNamedArray(L"elements");
    if(items.Size()>100)throw std::runtime_error("This scene exceeds the 100-text-element limit.");
    std::set<std::wstring> ids;
    for(const auto& item:items){
        auto o=item.GetObject();TextElement t;
        if(o.GetNamedString(L"type")!=L"text")throw std::runtime_error("This file contains elements not supported by this editor yet.");
        t.id=o.GetNamedString(L"id");t.text=o.GetNamedString(L"text");t.family=o.GetNamedString(L"family");
        t.x=o.GetNamedNumber(L"x");t.y=o.GetNamedNumber(L"y");t.width=o.GetNamedNumber(L"width");t.height=o.GetNamedNumber(L"height");
        t.size=o.GetNamedNumber(L"size");t.bold=o.GetNamedBoolean(L"bold");
        double color=o.GetNamedNumber(L"color"),alignment=o.GetNamedNumber(L"alignment");
        if(!std::isfinite(color)||color<0||color>0xffffff||color!=std::floor(color)||
           !std::isfinite(alignment)||alignment<0||alignment>2||alignment!=std::floor(alignment))throw std::runtime_error("Invalid text style.");
        t.color=static_cast<uint32_t>(color);t.alignment=static_cast<int>(alignment);
        if(t.id.empty()||!ids.insert(t.id).second||t.text.size()>20000||t.family.empty()||t.family.size()>128||
           !std::isfinite(t.x)||!std::isfinite(t.y)||!std::isfinite(t.width)||!std::isfinite(t.height)||!std::isfinite(t.size)||
           t.x<0||t.y<0||t.width<.01||t.height<.01||t.x+t.width>1.000001||t.y+t.height>1.000001||t.size<8||t.size>300)
            throw std::runtime_error("Invalid text geometry or content.");
        scene.texts.push_back(std::move(t));
    }
    return scene;
}
inline void SaveScene(const std::filesystem::path& path,const Scene& scene){
    auto bytes=EncodeScene(scene);
    auto temporary=path;temporary+=L"."+NewId()+L".tmp";
    try{
        Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(!file)throw HRESULT_FROM_WIN32(GetLastError());DWORD written=0;
        if(!WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size())throw HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
        if(!FlushFileBuffers(file.value))throw HRESULT_FROM_WIN32(GetLastError());file.reset();
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw HRESULT_FROM_WIN32(GetLastError());
    }catch(...){DeleteFileW(temporary.c_str());throw;}
}
inline Scene LoadScene(const std::filesystem::path& path){
    Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!file)throw HRESULT_FROM_WIN32(GetLastError());LARGE_INTEGER size;
    if(!GetFileSizeEx(file.value,&size))throw HRESULT_FROM_WIN32(GetLastError());
    if(size.QuadPart<0||size.QuadPart>8*1024*1024)throw std::runtime_error("This deck is too large to open.");
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD read=0;
    if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)||read!=bytes.size())throw HRESULT_FROM_WIN32(ERROR_READ_FAULT);
    return DecodeScene(bytes);
}
}
