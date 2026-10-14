#include "generated_card.h"
#include <cmath>
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
    return !c.id.empty() && c.id.size()<=64 && !c.name.empty() && c.name.size()<=80 && c.name.find_first_of(L"\r\n\t")==std::wstring::npos &&
        !c.html.empty() && winrt::to_string(c.html).size()<=131072 && std::isfinite(c.width)&&c.width>=180&&c.width<=1280 &&
        std::isfinite(c.height)&&c.height>=100&&c.height<=720 && std::isfinite(c.x)&&std::isfinite(c.y)&&std::isfinite(c.w)&&std::isfinite(c.h)&&
        c.x>=0&&c.y>=0&&c.w>0&&c.h>0&&c.x+c.w<=1280.1&&c.y+c.h<=720.1 && ValidCardState(c.state);
}
std::wstring GeneratedCardsJson(const std::vector<GeneratedCard>& cards){JsonArray list;for(auto& c:cards){JsonObject obj;
    for(auto [key,value]:{std::pair{L"id",c.id},std::pair{L"name",c.name},std::pair{L"html",c.html},std::pair{L"state",c.state}})obj.SetNamedValue(key,JsonValue::CreateStringValue(value));
    for(auto [key,value]:{std::pair{L"width",c.width},std::pair{L"height",c.height},std::pair{L"x",c.x},std::pair{L"y",c.y},std::pair{L"w",c.w},std::pair{L"h",c.h}})obj.SetNamedValue(key,JsonValue::CreateNumberValue(value));
    obj.SetNamedValue(L"visible",JsonValue::CreateBooleanValue(c.visible));list.Append(obj);}return std::wstring(list.Stringify());}
bool ParseGeneratedCards(const std::wstring& json,std::vector<GeneratedCard>& cards){try{auto list=JsonArray::Parse(json);if(list.Size()>16)return false;std::vector<GeneratedCard> result;std::set<std::wstring> ids;
    for(auto item:list){auto obj=item.GetObject();GeneratedCard c;c.id=obj.GetNamedString(L"id");c.name=obj.GetNamedString(L"name");c.html=obj.GetNamedString(L"html");c.state=obj.GetNamedString(L"state");
        c.width=obj.GetNamedNumber(L"width");c.height=obj.GetNamedNumber(L"height");c.x=obj.GetNamedNumber(L"x");c.y=obj.GetNamedNumber(L"y");c.w=obj.GetNamedNumber(L"w");c.h=obj.GetNamedNumber(L"h");c.visible=obj.GetNamedBoolean(L"visible");
        if(obj.Size()!=11 || !ValidGeneratedCard(c) || !ids.insert(c.id).second)return false;result.push_back(std::move(c));}cards=std::move(result);return true;
    }catch(...){return false;}}
}
