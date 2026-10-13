#pragma once
#include "editor.h"

namespace callayers {
inline std::wstring SetupHtmlText(const std::wstring& text){
    std::wstring result;
    for(auto ch:text)switch(ch){case L'&':result+=L"&amp;";break;case L'<':result+=L"&lt;";break;case L'>':result+=L"&gt;";break;case L'\"':result+=L"&quot;";break;default:result+=ch;}
    return result;
}
inline CardElement NameTagCard(const std::wstring& name,const std::wstring& detail){
    CardElement card;card.id=NewId();card.name=L"Name tag";card.x=.05;card.y=.76;card.width=.46;card.height=.17;
    card.html=L"<!doctype html><html><head><style>*{box-sizing:border-box}html,body{margin:0;width:100%;height:100%;background:transparent}body{display:flex;align-items:center;font-family:Segoe UI,sans-serif;padding:8px}.tag{max-width:100%;padding:8px 20px;border-left:4px solid #b7afff;color:white;text-shadow:0 2px 5px #000b,0 0 18px #0008;overflow:hidden}.name{font-size:clamp(24px,5vw,42px);font-weight:650;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.detail{font-size:clamp(16px,3vw,26px);color:#c9f3e6;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.detail:empty{display:none}</style></head><body><div class='tag'><div class='name'>"+SetupHtmlText(name)+L"</div><div class='detail'>"+SetupHtmlText(detail)+L"</div></div></body></html>";
    return card;
}
struct SetupProgress {int step=0,camera=-1,app=0;std::wstring name,detail,cardId;};
inline std::wstring EncodeSetupProgress(const SetupProgress& progress,bool completed=false){
    using namespace winrt::Windows::Data::Json;
    JsonObject data;data.SetNamedValue(L"version",JsonValue::CreateNumberValue(1));
    data.SetNamedValue(L"completed",JsonValue::CreateBooleanValue(completed));
    data.SetNamedValue(L"step",JsonValue::CreateNumberValue(progress.step));
    data.SetNamedValue(L"app",JsonValue::CreateNumberValue(progress.app));
    data.SetNamedValue(L"name",JsonValue::CreateStringValue(progress.name));data.SetNamedValue(L"detail",JsonValue::CreateStringValue(progress.detail));data.SetNamedValue(L"cardId",JsonValue::CreateStringValue(progress.cardId));
    return std::wstring(data.Stringify());
}
inline SetupProgress DecodeSetupProgress(const std::wstring& text){
    try{
        auto data=winrt::Windows::Data::Json::JsonObject::Parse(text);SetupProgress p;
        p.step=std::clamp(int(data.GetNamedNumber(L"step",0)),0,2);p.app=std::clamp(int(data.GetNamedNumber(L"app",0)),0,3);
        p.name=data.GetNamedString(L"name",L"");p.detail=data.GetNamedString(L"detail",L"");p.cardId=data.GetNamedString(L"cardId",L"");return p;
    }catch(...){return {};}
}
inline bool LiveDeliveryRecent(ULONGLONG now,ULONGLONG delivered){return delivered!=0&&now>=delivered&&now-delivered<2000;}
}
