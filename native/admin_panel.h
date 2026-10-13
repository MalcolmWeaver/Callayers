#pragma once
#include "cloud_client.h"
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <functional>

namespace callayers {
namespace adminx=winrt::Microsoft::UI::Xaml;
namespace adminc=adminx::Controls;
class AdminPanel {
    using Json=winrt::Windows::Data::Json::JsonObject;
    using Value=winrt::Windows::Data::Json::JsonValue;
    enum class Operation { Spending, Accounts, Detail, Save, Toggle } operation=Operation::Spending;
    CloudAdminJob job;
    std::wstring owner=CloudGetAccount().uid,selected;
    bool busy=false,denied=false;
    double cursor=0;
    adminc::StackPanel work,accounts,details,requests;
    // StackPanel is a layout element. A Control supplies inherited IsEnabled
    // for both pointer and keyboard interaction across all admin inputs.
    adminc::ContentControl workHost;
    adminc::TextBlock status,spending,summary;
    adminc::TextBox email;
    adminc::CheckBox paid,disabled;
    adminc::Button more;
    static std::wstring Money(double dollars){wchar_t value[80];swprintf_s(value,L"$%.4f",dollars);return value;}
    static std::wstring String(const Json& data,const wchar_t* key){return std::wstring(data.GetNamedString(key,L""));}
    static std::wstring Encode(const std::wstring& text){
        constexpr wchar_t hex[]=L"0123456789ABCDEF";std::wstring out;
        for(unsigned char c:winrt::to_string(text)){
            if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=wchar_t(c);
            else{out+=L'%';out+=hex[c>>4];out+=hex[c&15];}
        }return out;
    }
    static std::wstring Date(double milliseconds){
        if(milliseconds<=0)return L"not started";
        ULARGE_INTEGER ticks;ticks.QuadPart=static_cast<ULONGLONG>(milliseconds*10000)+116444736000000000ULL;
        FILETIME file{ticks.LowPart,ticks.HighPart};SYSTEMTIME utc{};
        if(!FileTimeToSystemTime(&file,&utc))return L"unknown";
        wchar_t text[64];swprintf_s(text,L"%04u-%02u-%02u %02u:%02u UTC",utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute);return text;
    }
    static adminc::TextBlock Text(const std::wstring& text){adminc::TextBlock item;item.Text(text);item.TextWrapping(adminx::TextWrapping::Wrap);return item;}
    adminc::Button Button(adminc::StackPanel const& panel,const wchar_t* label,std::function<void()> action){
        adminc::Button button;button.Content(winrt::box_value(label));
        button.Click([this,action](auto&&,auto&&){try{action();}catch(const std::exception& e){status.Text(winrt::to_hstring(e.what()));}catch(...){status.Text(L"Could not complete the action. Refresh before retrying.");}});
        panel.Children().Append(button);return button;
    }
    void Begin(Operation op,const std::wstring& path,const std::wstring& method=L"GET",const std::string& body={}){
        if(busy||denied)return;
        job.Start(method,path,body);operation=op;busy=true;workHost.IsEnabled(false);status.Text(L"Loading…");
    }
    void Spending(){Begin(Operation::Spending,L"/v1/admin/spending");}
    void Accounts(const std::wstring& query=L""){Begin(Operation::Accounts,L"/v1/admin/accounts"+query);}
    void Detail(){Begin(Operation::Detail,L"/v1/admin/accounts/"+Encode(selected));}
    void Rows(const Json& data){
        accounts.Children().Clear();details.Visibility(adminx::Visibility::Collapsed);selected.clear();
        for(auto row:data.GetNamedArray(L"accounts")){
            auto a=row.GetObject();auto uid=String(a,L"uid");
            std::wstring access=a.GetNamedBoolean(L"disabled",false)?L"Disabled":a.GetNamedBoolean(L"paid",false)?L"Paid":L"AI trial";
            auto label=String(a,L"email")+L"\n"+access+L" · Lifetime AI "+Money(a.GetNamedNumber(L"totalSpent",0)/1000000.);
            auto button=Button(accounts,L"",[this,uid]{selected=uid;Detail();});button.Content(Text(label));button.HorizontalAlignment(adminx::HorizontalAlignment::Stretch);
        }
        if(accounts.Children().Size()==0)accounts.Children().Append(Text(L"No accounts found."));
    }
    void ShowDetail(const Json& a){
        summary.Text(String(a,L"email")+L"\nAccount ID: "+selected+L"\nRemaining allowance: "+Money(a.GetNamedNumber(L"remainingUsd",0))+L" · Monthly spent: "+Money(a.GetNamedNumber(L"monthlySpentUsd",0)));
        auto trial=a.GetNamedValue(L"trialEndsAt");
        summary.Text(std::wstring(summary.Text())+L"\nTrial ends: "+(trial.ValueType()==winrt::Windows::Data::Json::JsonValueType::Number?Date(trial.GetNumber()):L"not started"));
        paid.IsChecked(a.GetNamedBoolean(L"paid"));disabled.IsChecked(a.GetNamedBoolean(L"disabled"));
        disabled.IsEnabled(selected!=owner);requests.Children().Clear();
        requests.Children().Append(Text(L"Recent AI requests (up to 50; amounts are estimated USD)"));
        for(auto row:a.GetNamedArray(L"requests")){
            auto r=row.GetObject();auto text=String(r,L"requestId")+L"\n"+String(r,L"state")+L" · "+Money(r.GetNamedNumber(L"estimatedCostMicros",r.GetNamedNumber(L"reserved",0))/1000000.);
            if(r.GetNamedBoolean(L"uncertain",false))text+=L" (reservation; outcome uncertain)";
            auto usage=r.GetNamedValue(L"usage",nullptr);
            if(usage&&usage.ValueType()==winrt::Windows::Data::Json::JsonValueType::Object){auto u=usage.GetObject();text+=L"\nTokens in / out: "+std::to_wstring(static_cast<int64_t>(u.GetNamedNumber(L"input_tokens",0)))+L" / "+std::to_wstring(static_cast<int64_t>(u.GetNamedNumber(L"output_tokens",0)));}
            text+=L"\n"+Date(r.GetNamedNumber(L"createdAt",0));requests.Children().Append(Text(text));
        }
        details.Visibility(adminx::Visibility::Visible);
    }
    void Deny(){denied=true;job.Cancel();accounts.Children().Clear();requests.Children().Clear();summary.Text(L"");spending.Text(L"");details.Visibility(adminx::Visibility::Collapsed);workHost.IsEnabled(false);status.Text(L"Administrator access is unavailable. Close this panel and refresh your account.");}
public:
    adminc::ScrollViewer Root;
    AdminPanel(){
        Root.MaxHeight(480);Root.MinWidth(420);Root.HorizontalScrollBarVisibility(adminc::ScrollBarVisibility::Disabled);
        adminc::StackPanel panel;panel.Spacing(12);Root.Content(panel);panel.Children().Append(status);workHost.Content(work);workHost.HorizontalContentAlignment(adminx::HorizontalAlignment::Stretch);panel.Children().Append(workHost);work.Spacing(12);
        spending.TextWrapping(adminx::TextWrapping::Wrap);work.Children().Append(spending);
        work.Children().Append(Text(L"The pause switch stops new AI generations for everyone. Resuming still respects deployment settings and spending limits."));
        adminc::StackPanel switches;switches.Orientation(adminc::Orientation::Horizontal);switches.Spacing(8);work.Children().Append(switches);
        Button(switches,L"Pause AI",[this]{Begin(Operation::Toggle,L"/v1/admin/ai",L"POST","{\"disabled\":true}");});
        Button(switches,L"Resume AI",[this]{Begin(Operation::Toggle,L"/v1/admin/ai",L"POST","{\"disabled\":false}");});
        email.Header(winrt::box_value(L"Find account by exact Google email"));email.MaxLength(320);work.Children().Append(email);
        adminc::StackPanel navigation;navigation.Orientation(adminc::Orientation::Horizontal);navigation.Spacing(8);work.Children().Append(navigation);
        Button(navigation,L"Search",[this]{auto value=std::wstring(email.Text());if(value.empty()){Accounts();return;}Accounts(L"?email="+Encode(value));});
        Button(navigation,L"Recent",[this]{Accounts();});Button(navigation,L"Top spenders",[this]{Spending();});
        more=Button(navigation,L"Next",[this]{if(cursor>0)Accounts(L"?cursor="+std::to_wstring(static_cast<int64_t>(cursor)));});more.Visibility(adminx::Visibility::Collapsed);
        accounts.Spacing(6);details.Spacing(10);details.Visibility(adminx::Visibility::Collapsed);work.Children().Append(details);work.Children().Append(accounts);
        summary.TextWrapping(adminx::TextWrapping::Wrap);details.Children().Append(summary);
        paid.Content(winrt::box_value(L"Paid access (manual; no payment is collected)"));disabled.Content(winrt::box_value(L"Disable account"));
        details.Children().Append(paid);details.Children().Append(disabled);
        Button(details,L"Save access",[this]{Json change;change.SetNamedValue(L"paid",Value::CreateBooleanValue(paid.IsChecked().Value()));change.SetNamedValue(L"disabled",Value::CreateBooleanValue(disabled.IsChecked().Value()));Begin(Operation::Save,L"/v1/admin/accounts/"+Encode(selected),L"PATCH",winrt::to_string(change.Stringify()));});
        Button(details,L"Refresh details",[this]{Detail();});requests.Spacing(10);details.Children().Append(requests);Spending();
    }
    void Poll(){
        if(denied)return;auto account=CloudGetAccount();if(!account.signedIn||!account.administrator||account.uid!=owner){Deny();return;}
        auto result=job.TakeResult();if(!result)return;busy=false;workHost.IsEnabled(true);
        if(!result->error.empty()){status.Text(result->error+L" Refresh before retrying a change.");return;}
        try{
            auto data=Json::Parse(winrt::to_hstring(result->json));status.Text(L"Updated.");
            switch(operation){
            case Operation::Spending:{
                auto global=data.GetNamedObject(L"global");spending.Text(String(data,L"period")+L" UTC: "+Money(global.GetNamedNumber(L"spent",0)/1000000.)+L" spent + "+Money(global.GetNamedNumber(L"reserved",0)/1000000.)+L" reserved / "+Money(data.GetNamedNumber(L"globalLimitUsd"))+L" limit. AI "+(data.GetNamedBoolean(L"paused")?L"paused.":L"not manually paused."));
                cursor=0;more.Visibility(adminx::Visibility::Collapsed);Rows(data);break;}
            case Operation::Accounts:{auto next=data.GetNamedValue(L"nextCursor");cursor=next.ValueType()==winrt::Windows::Data::Json::JsonValueType::Number?next.GetNumber():0;more.Visibility(cursor>0?adminx::Visibility::Visible:adminx::Visibility::Collapsed);Rows(data);break;}
            case Operation::Detail:ShowDetail(data);break;
            case Operation::Save:CloudRefresh();Detail();break;
            case Operation::Toggle:Spending();break;
            }
        }catch(...){status.Text(L"Could not display the response. Refresh before retrying a change.");}
    }
};
}
