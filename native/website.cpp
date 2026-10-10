#include "common.h"
#include "website.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <winrt/Windows.Data.Html.h>
#include <cwctype>
#include <regex>

namespace callayers {
namespace {
std::wstring Lower(std::wstring s){for(auto& c:s)c=static_cast<wchar_t>(towlower(c));return s;}
struct WebHandle {
    HINTERNET h;
    explicit WebHandle(HINTERNET value):h(value){if(!h)throw std::runtime_error("Could not open the website connection.");}
    ~WebHandle(){WinHttpCloseHandle(h);}
    WebHandle(const WebHandle&)=delete;
};
void Require(BOOL ok){if(!ok)throw std::runtime_error("Could not read the website. Check the URL and connection, or paste the relevant text instead.");}
struct Address {std::wstring host,path;};
Address Parse(const std::wstring& url){
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS||parts.nPort!=443||parts.dwUserNameLength||parts.dwPasswordLength)
        throw std::runtime_error("Use a public https:// page without a username, password, or custom port.");
    Address a{Lower(std::wstring(parts.lpszHostName,parts.dwHostNameLength)),std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength)};
    if(a.host.find(L'.')==std::wstring::npos||a.host.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789.-")!=std::wstring::npos||
        a.host.ends_with(L".localhost")||a.host.ends_with(L".local")||a.host.ends_with(L".internal")||
        a.host.find_first_not_of(L"0123456789.")==std::wstring::npos)
        throw std::runtime_error("Use a public website hostname, not a local address.");
    if(a.path.empty())a.path=L"/";
    if(parts.dwExtraInfoLength)a.path+=std::wstring(parts.lpszExtraInfo,parts.dwExtraInfoLength);if(auto hash=a.path.find(L'#');hash!=std::wstring::npos)a.path.resize(hash);
    return a;
}
std::wstring Header(HINTERNET request,DWORD name){
    DWORD bytes=0;WinHttpQueryHeaders(request,name,nullptr,nullptr,&bytes,nullptr);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER)return {};
    std::wstring result(bytes/sizeof(wchar_t),L'\0');Require(WinHttpQueryHeaders(request,name,nullptr,result.data(),&bytes,nullptr));result.resize(bytes/sizeof(wchar_t));return result;
}
}
std::wstring WebsiteUrl(const std::wstring& prompt){
    // Only explicit URLs in the user's prompt initiate network access. Never follow
    // links suggested by retrieved content, generated HTML, or model output.
    static const std::wregex pattern(LR"(https?://[^\s<>"']+)",std::regex::icase);
    std::wstring result;
    for(std::wsregex_iterator it(prompt.begin(),prompt.end(),pattern),end;it!=end;++it){
        auto url=it->str();while(!url.empty()&&std::wstring_view(L".,;!?)］]").find(url.back())!=std::wstring_view::npos)url.pop_back();
        Parse(url);if(!result.empty()&&result!=url)throw std::runtime_error("Summarize one website URL at a time.");result=std::move(url);
    }
    return result;
}
std::wstring WebsiteText(const std::wstring& html){
    auto lower=Lower(html);std::wstring cleaned;cleaned.reserve(html.size());
    // Discard non-reading content before Windows' HTML-to-text conversion.
    for(size_t i=0;i<html.size();){
        if(lower.compare(i,4,L"<!--")==0){auto end=lower.find(L"-->",i+4);i=end==std::wstring::npos?html.size():end+3;continue;}
        bool skipped=false;
        for(auto tag:{L"script",L"style",L"noscript",L"svg",L"template"}){
            std::wstring opening=L"<"+std::wstring(tag);size_t after=i+opening.size();
            if(lower.compare(i,opening.size(),opening)==0&&after<lower.size()&&(iswspace(lower[after])||lower[after]==L'>')){
                auto end=lower.find(L"</"+std::wstring(tag),after);end=end==std::wstring::npos?end:lower.find(L'>',end);
                i=end==std::wstring::npos?html.size():end+1;cleaned+=L' ';skipped=true;break;
            }
        }
        if(!skipped)cleaned+=html[i++];
    }
    auto plain=winrt::Windows::Data::Html::HtmlUtilities::ConvertToText(cleaned);std::wstring result;bool space=false;
    for(wchar_t c:plain){if(iswspace(c)||c==0xa0){space=!result.empty();continue;}if(space)result+=L' ';space=false;if(c>=32)result+=c;}
    return result;
}
WebsiteSource FetchWebsite(const std::wstring& original,const std::atomic<bool>& canceled){
    WebHandle session(WinHttpOpen(L"Callayers/0.4 WebsiteReader",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0));
    Require(WinHttpSetTimeouts(session.h,10000,10000,10000,15000));
    std::wstring url=original;const auto deadline=Clock::now()+std::chrono::seconds(45);
    auto check=[&]{if(canceled)throw std::runtime_error("Website reading canceled.");if(Clock::now()>deadline)throw std::runtime_error("The website took too long to read. Retry or paste its text.");};
    for(int redirects=0;redirects<=4;++redirects){
        check();auto address=Parse(url);
        WebHandle connection(WinHttpConnect(session.h,address.host.c_str(),443,0));
        WebHandle request(WinHttpOpenRequest(connection.h,L"GET",address.path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;Require(WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)));
        DWORD disabled=WINHTTP_DISABLE_COOKIES;Require(WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)));
        DWORD logon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;Require(WinHttpSetOption(request.h,WINHTTP_OPTION_AUTOLOGON_POLICY,&logon,sizeof(logon)));
        DWORD decompress=WINHTTP_DECOMPRESSION_FLAG_GZIP|WINHTTP_DECOMPRESSION_FLAG_DEFLATE;Require(WinHttpSetOption(request.h,WINHTTP_OPTION_DECOMPRESSION,&decompress,sizeof(decompress)));
        Require(WinHttpSendRequest(request.h,L"Accept: text/html, text/plain\r\n",static_cast<DWORD>(-1),nullptr,0,0,0));Require(WinHttpReceiveResponse(request.h,nullptr));check();
        DWORD status=0,bytes=sizeof(status);Require(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&bytes,nullptr));
        if(status==301||status==302||status==303||status==307||status==308){
            auto location=Header(request.h,WINHTTP_QUERY_LOCATION);wchar_t combined[8192];DWORD length=8192;
            if(location.empty()||FAILED(UrlCombineW(url.c_str(),location.c_str(),combined,&length,0)))throw std::runtime_error("The website returned an unreadable redirect.");
            url=combined;continue;
        }
        if(status!=200)throw std::runtime_error("The website could not be read (it may require login or block automated access). Paste its text instead.");
        auto type=Lower(Header(request.h,WINHTTP_QUERY_CONTENT_TYPE));bool html=type.starts_with(L"text/html")||type.starts_with(L"application/xhtml+xml");
        if(!html&&!type.starts_with(L"text/plain"))throw std::runtime_error("Use an HTML or text page. PDF and other downloads are not supported yet.");
        std::string content;char buffer[16384];DWORD read=0;
        do{check();Require(WinHttpReadData(request.h,buffer,sizeof(buffer),&read));if(content.size()+read>1024*1024)throw std::runtime_error("This page is too large. Use a shorter article or paste an excerpt.");content.append(buffer,read);}while(read);
        UINT encoding=(type.find(L"windows-1252")!=std::wstring::npos||type.find(L"iso-8859-1")!=std::wstring::npos)?1252:CP_UTF8;
        int count=MultiByteToWideChar(encoding,encoding==CP_UTF8?MB_ERR_INVALID_CHARS:0,content.data(),static_cast<int>(content.size()),nullptr,0);
        if(!count)throw std::runtime_error("The website's text encoding could not be read. Paste the text instead.");
        std::wstring decoded(count,L'\0');MultiByteToWideChar(encoding,0,content.data(),static_cast<int>(content.size()),decoded.data(),count);
        auto text=html?WebsiteText(decoded):decoded;
        if(text.size()<100)throw std::runtime_error("This page has too little readable text; it may need JavaScript or login. Paste the relevant text instead.");
        bool truncated=text.size()>24000;if(truncated)text.resize(24000);check();return {url,std::move(text),truncated};
    }
    throw std::runtime_error("The website redirected too many times.");
}
}
