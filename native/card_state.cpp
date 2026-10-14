#include "card_state.h"
#include <cmath>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
namespace callayers {
bool ParseInteractiveState(const std::wstring& json,InteractiveState& result){
    if(json.size()>4096)return false;
    try{auto value=winrt::Windows::Data::Json::JsonObject::Parse(json);
        if(value.Size()!=3)return false;
        double count=value.GetNamedNumber(L"count"),remaining=value.GetNamedNumber(L"remaining");
        auto title=value.GetNamedString(L"title");
        if(!std::isfinite(count)||count<0||count>9999||std::floor(count)!=count||!std::isfinite(remaining)||remaining<0||remaining>300||title.size()>120)return false;
        result={int(count),std::wstring(title),remaining};return true;
    }catch(...){return false;}
}
std::wstring InteractiveStateJson(const InteractiveState& state){
    using namespace winrt::Windows::Data::Json;
    JsonObject value;value.SetNamedValue(L"count",JsonValue::CreateNumberValue(state.count));
    value.SetNamedValue(L"title",JsonValue::CreateStringValue(state.title));
    value.SetNamedValue(L"remaining",JsonValue::CreateNumberValue(state.remaining));return std::wstring(value.Stringify());
}
}
