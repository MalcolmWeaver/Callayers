#include "generated_card.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
namespace callayers {
using namespace winrt::Windows::Data::Json;
bool bounded(const IJsonValue& value,int depth){
    if(depth>4)return false;
    switch(value.ValueType()){
    case JsonValueType::Null:case JsonValueType::Boolean:return true;
    case JsonValueType::Number:return std::isfinite(value.GetNumber());
    case JsonValueType::String:return value.GetString().size()<=4096;
    case JsonValueType::Array:{auto list=value.GetArray();if(list.Size()>128)return false;for(auto child:list)if(!bounded(child,depth+1))return false;return true;}
    case JsonValueType::Object:{auto obj=value.GetObject();if(obj.Size()>64)return false;for(auto child:obj){auto key=child.Key();if(key.size()>80||key==L"__proto__"||key==L"prototype"||key==L"constructor"||!bounded(child.Value(),depth+1))return false;}return true;}
    }return false;
}
bool ValidCardState(const std::wstring& json){if(winrt::to_string(json).size()>16384)return false;try{return bounded(JsonObject::Parse(json),0);}catch(...){return false;}}
bool ValidGeneratedCard(const GeneratedCard& c){
    bool image=!c.image.empty();
    if(image && !c.html.empty() && c.html.find(L"callayers-image:local")==std::wstring::npos)return false;
    if(image && (c.image.size()>1400000 || c.image.rfind(L"data:image/png;base64,",0)!=0 || c.image.size()<30 || c.image.substr(22).find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")!=std::wstring::npos))return false;
    return !c.id.empty() && c.id.size()<=64 && !c.name.empty() && c.name.size()<=80 && c.name.find_first_of(L"\r\n\t")==std::wstring::npos &&
        (image || !c.html.empty()) && winrt::to_string(c.html).size()<=131072 && std::isfinite(c.width)&&c.width>=(image?1:180)&&c.width<=1280 &&
        std::isfinite(c.height)&&c.height>=(image?1:100)&&c.height<=720 && std::isfinite(c.x)&&std::isfinite(c.y)&&std::isfinite(c.w)&&std::isfinite(c.h)&&
        c.x>=0&&c.y>=0&&c.w>0&&c.h>0&&c.x+c.w<=1280.1&&c.y+c.h<=720.1 && c.layer>=0 && c.layer<16 && ValidCardState(c.state);
}
std::wstring CardDocument(const GeneratedCard& c){
    if(c.image.empty())return c.html;
    auto html=c.html.empty()?L"<img alt=\"Imported image\" draggable=\"false\" style=\"display:block;width:100%;height:100%;object-fit:contain\" src=\"callayers-image:local\">":c.html;
    std::wstring marker=L"callayers-image:local";size_t at=0;while((at=html.find(marker,at))!=std::wstring::npos){html.replace(at,marker.size(),c.image);at+=c.image.size();}return html;
}
std::wstring RevisionInput(const GeneratedCard& c){JsonObject obj;obj.SetNamedValue(L"name",JsonValue::CreateStringValue(c.name));obj.SetNamedValue(L"html",JsonValue::CreateStringValue(c.image.empty()?c.html:c.html.empty()?L"<img src=\"callayers-image:local\" style=\"width:100%;height:100%;object-fit:contain\">":c.html));
    obj.SetNamedValue(L"width",JsonValue::CreateNumberValue(c.width));obj.SetNamedValue(L"height",JsonValue::CreateNumberValue(c.height));JsonArray ids;if(!c.image.empty())ids.Append(JsonValue::CreateStringValue(L"local"));obj.SetNamedValue(L"imageIds",ids);return std::wstring(obj.Stringify());}
static void fitReplacement(GeneratedCard& c){double scale=std::min({c.w/c.width,(1280-c.x)/c.width,(720-c.y)/c.height});c.w=c.width*scale;c.h=c.height*scale;}
bool MatchesRevisionTarget(const GeneratedCard& current,const std::wstring& id,unsigned long long version){return current.id==id && current.contentVersion==version;}
bool RevisedCard(const GeneratedCard& current,const GeneratedCard& result,GeneratedCard& replacement){
    auto next=current;next.previous=CardContent{current.name,current.html,current.width,current.height};next.html=result.html;next.width=result.width;next.height=result.height;next.state=L"{}";next.restored=false;next.lastRunning=false;++next.contentVersion;fitReplacement(next);
    if(!ValidGeneratedCard(next))return false;replacement=std::move(next);return true;
}
bool RestorePrevious(const GeneratedCard& current,GeneratedCard& replacement){if(!current.previous)return false;auto next=current;next.name=next.previous->name;next.html=next.previous->html;next.width=next.previous->width;next.height=next.previous->height;next.previous.reset();next.state=L"{}";next.restored=false;next.lastRunning=false;++next.contentVersion;fitReplacement(next);if(!ValidGeneratedCard(next))return false;replacement=std::move(next);return true;}
std::wstring GeneratedCardsJson(const std::vector<GeneratedCard>& cards){JsonArray list;for(auto& c:cards){JsonObject obj;
    for(auto [key,value]:{std::pair{L"id",c.id},std::pair{L"name",c.name},std::pair{L"html",c.html},std::pair{L"state",c.state}})obj.SetNamedValue(key,JsonValue::CreateStringValue(value));
    for(auto [key,value]:{std::pair{L"width",c.width},std::pair{L"height",c.height},std::pair{L"x",c.x},std::pair{L"y",c.y},std::pair{L"w",c.w},std::pair{L"h",c.h}})obj.SetNamedValue(key,JsonValue::CreateNumberValue(value));
    obj.SetNamedValue(L"visible",JsonValue::CreateBooleanValue(c.visible));if(!c.image.empty())obj.SetNamedValue(L"image",JsonValue::CreateStringValue(c.image));obj.SetNamedValue(L"layer",JsonValue::CreateNumberValue(c.layer));obj.SetNamedValue(L"edited",JsonValue::CreateNumberValue(double(c.edited)));if(c.previous){JsonObject previous;previous.SetNamedValue(L"name",JsonValue::CreateStringValue(c.previous->name));previous.SetNamedValue(L"html",JsonValue::CreateStringValue(c.previous->html));previous.SetNamedValue(L"width",JsonValue::CreateNumberValue(c.previous->width));previous.SetNamedValue(L"height",JsonValue::CreateNumberValue(c.previous->height));obj.SetNamedValue(L"previous",previous);}list.Append(obj);}return std::wstring(list.Stringify());}
bool ParseGeneratedCards(const std::wstring& json,std::vector<GeneratedCard>& cards){try{auto list=JsonArray::Parse(json);if(list.Size()>16)return false;std::vector<GeneratedCard> result;std::set<std::wstring> ids;
    for(auto item:list){auto obj=item.GetObject();GeneratedCard c;c.id=obj.GetNamedString(L"id");c.name=obj.GetNamedString(L"name");c.html=obj.GetNamedString(L"html");c.state=obj.GetNamedString(L"state");
        c.width=obj.GetNamedNumber(L"width");c.height=obj.GetNamedNumber(L"height");c.x=obj.GetNamedNumber(L"x");c.y=obj.GetNamedNumber(L"y");c.w=obj.GetNamedNumber(L"w");c.h=obj.GetNamedNumber(L"h");c.visible=obj.GetNamedBoolean(L"visible");
        c.image=obj.GetNamedString(L"image",L"");
        double layer=obj.GetNamedNumber(L"layer",double(result.size()));if(!std::isfinite(layer)||layer<0||layer>=16||std::floor(layer)!=layer)return false;c.layer=int(layer);double edited=obj.GetNamedNumber(L"edited",0);if(!std::isfinite(edited)||edited<0||edited>9007199254740991.0||std::floor(edited)!=edited)return false;c.edited=static_cast<unsigned long long>(edited);
        if(obj.HasKey(L"previous")){auto p=obj.GetNamedObject(L"previous");if(p.Size()!=4)return false;c.previous=CardContent{std::wstring(p.GetNamedString(L"name")),std::wstring(p.GetNamedString(L"html")),p.GetNamedNumber(L"width"),p.GetNamedNumber(L"height")};auto backup=c;backup.name=c.previous->name;backup.html=c.previous->html;backup.width=c.previous->width;backup.height=c.previous->height;backup.previous.reset();if(!ValidGeneratedCard(backup))return false;}
        for(auto pair:obj)if(pair.Key()!=L"id" && pair.Key()!=L"name" && pair.Key()!=L"html" && pair.Key()!=L"state" && pair.Key()!=L"width" && pair.Key()!=L"height" && pair.Key()!=L"x" && pair.Key()!=L"y" && pair.Key()!=L"w" && pair.Key()!=L"h" && pair.Key()!=L"visible" && pair.Key()!=L"image" && pair.Key()!=L"layer" && pair.Key()!=L"edited" && pair.Key()!=L"previous")return false;
        if(!ValidGeneratedCard(c) || !ids.insert(c.id).second)return false;result.push_back(std::move(c));}cards=std::move(result);return true;
    }catch(...){return false;}}
}
