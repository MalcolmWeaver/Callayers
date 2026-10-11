#include <winsock2.h>
#include <ws2tcpip.h>
#include "common.h"
#include "website.h"
#include "image_asset.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <cwctype>
#include <regex>
#include <map>
#include <winrt/base.h>

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
size_t TagEnd(const std::wstring& html,size_t start){
    wchar_t quote=0;
    for(size_t i=start;i<html.size();++i){
        wchar_t c=html[i];
        if(quote){if(c==quote)quote=0;}
        else if(c==L'\''||c==L'"')quote=c;
        else if(c==L'>')return i;
    }
    return std::wstring::npos;
}
bool IsTagCharacter(wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L':';}
std::wstring DecodeEntity(std::wstring_view entity){
    if(entity.starts_with(L"#")){
        size_t i=1;unsigned base=10,value=0;
        if(i<entity.size()&&(entity[i]==L'x'||entity[i]==L'X')){base=16;++i;}
        if(i==entity.size())return {};
        for(;i<entity.size();++i){
            wchar_t c=entity[i];unsigned digit=c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:c>=L'A'&&c<=L'F'?c-L'A'+10:base;
            if(digit>=base||value>(0x10ffff-digit)/base)return {};
            value=value*base+digit;
        }
        if(!value||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return L"\ufffd";
        if(value<=0xffff)return std::wstring(1,static_cast<wchar_t>(value));
        value-=0x10000;return {static_cast<wchar_t>(0xd800+(value>>10)),static_cast<wchar_t>(0xdc00+(value&1023))};
    }
    static constexpr std::pair<std::wstring_view,std::wstring_view> named[]{
        {L"amp",L"&"},{L"AMP",L"&"},{L"lt",L"<"},{L"LT",L"<"},{L"gt",L">"},{L"GT",L">"},{L"quot",L"\""},{L"QUOT",L"\""},{L"apos",L"'"},
        {L"nbsp",L" "},{L"ensp",L" "},{L"emsp",L" "},{L"thinsp",L" "},{L"ndash",L"\u2013"},{L"mdash",L"\u2014"},
        {L"lsquo",L"\u2018"},{L"rsquo",L"\u2019"},{L"ldquo",L"\u201c"},{L"rdquo",L"\u201d"},{L"hellip",L"\u2026"},
        {L"copy",L"\u00a9"},{L"reg",L"\u00ae"},{L"trade",L"\u2122"},{L"bull",L"\u2022"},{L"middot",L"\u00b7"},
        {L"euro",L"\u20ac"},{L"pound",L"\u00a3"},{L"yen",L"\u00a5"},{L"cent",L"\u00a2"},
        {L"times",L"\u00d7"},{L"divide",L"\u00f7"},{L"plusmn",L"\u00b1"},{L"deg",L"\u00b0"},
        {L"laquo",L"\u00ab"},{L"raquo",L"\u00bb"},{L"eacute",L"\u00e9"},{L"Eacute",L"\u00c9"}
    };
    for(const auto& [name,value]:named)if(entity==name)return std::wstring(value);
    return {}; // Preserve uncommon named entities literally instead of losing text.
}
std::wstring Trim(std::wstring s){auto first=s.find_first_not_of(L" \t\r\n");if(first==std::wstring::npos)return {};return s.substr(first,s.find_last_not_of(L" \t\r\n")-first+1);}
std::wstring DecodeAttributes(const std::wstring& text){
    std::wstring result;
    for(size_t i=0;i<text.size();++i){if(text[i]==L'&'){auto end=text.find(L';',i+1);if(end!=std::wstring::npos&&end-i<=32){auto value=DecodeEntity(std::wstring_view(text).substr(i+1,end-i-1));if(!value.empty()){result+=value;i=end;continue;}}}result+=text[i];}return result;
}
using Attributes=std::map<std::wstring,std::wstring>;
Attributes ReadAttributes(const std::wstring& html,size_t start,size_t end){
    Attributes result;
    while(start<end){
        while(start<end&&(iswspace(html[start])||html[start]==L'/'))++start;
        size_t begin=start;while(start<end&&!iswspace(html[start])&&html[start]!=L'='&&html[start]!=L'/')++start;
        if(begin==start){++start;continue;}auto name=Lower(html.substr(begin,start-begin));
        while(start<end&&iswspace(html[start]))++start;std::wstring value;
        if(start<end&&html[start]==L'='){
            ++start;while(start<end&&iswspace(html[start]))++start;
            if(start<end&&(html[start]==L'\''||html[start]==L'"')){auto quote=html[start++];begin=start;while(start<end&&html[start]!=quote)++start;value=html.substr(begin,start-begin);if(start<end)++start;}
            else{begin=start;while(start<end&&!iswspace(html[start]))++start;value=html.substr(begin,start-begin);}
        }
        if(result.size()<80)result.try_emplace(std::move(name),DecodeAttributes(value));
    }return result;
}
std::wstring Base64(const std::string& bytes){
    DWORD count=0;Check(CryptBinaryToStringW(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&count)?S_OK:E_FAIL);
    std::wstring result(count,L'\0');Check(CryptBinaryToStringW(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,result.data(),&count)?S_OK:E_FAIL);
    result.resize(count);while(!result.empty()&&result.back()==0)result.pop_back();return result;
}
std::wstring DecodeText(const std::string& content,const std::wstring& type){
    UINT encoding=(type.find(L"windows-1252")!=std::wstring::npos||type.find(L"iso-8859-1")!=std::wstring::npos)?1252:CP_UTF8;
    int count=MultiByteToWideChar(encoding,encoding==CP_UTF8?MB_ERR_INVALID_CHARS:0,content.data(),static_cast<int>(content.size()),nullptr,0);
    if(!count)throw std::runtime_error("The website's text encoding could not be read. Paste the text instead.");
    std::wstring text(count,L'\0');MultiByteToWideChar(encoding,0,content.data(),static_cast<int>(content.size()),text.data(),count);return text;
}
}
std::wstring WebsiteUrl(const std::wstring& prompt){
    // Only addresses in the user's prompt initiate retrieval. Recognize common
    // bare website domains without mistaking emails or filenames for websites.
    // Never follow addresses supplied by retrieved content or model output.
    static const std::wregex tokens(LR"([^\s<>"'\[\]()]+)");
    static const std::wregex bare(LR"((?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+(?:com|org|net|edu|gov|mil|int|io|ai|app|dev|co|uk|us|ca|au|de|fr|eu|info|biz|me|tv|xyz|site|online|tech|store)(?::[0-9]+)?(?:[/?#][^\s<>"']*)?)",std::regex::icase);
    std::wstring result;
    for(std::wsregex_iterator it(prompt.begin(),prompt.end(),tokens),end;it!=end;++it){
        auto token=it->str();
        // Keep support for URLs embedded in Markdown links and labels.
        auto lower=Lower(token);auto start=lower.find(L"https://");if(start==std::wstring::npos)start=lower.find(L"http://");
        std::wstring url;
        if(start!=std::wstring::npos)url=token.substr(start);
        else{url=token;while(!url.empty()&&std::wstring_view(L"([{").find(url.front())!=std::wstring_view::npos)url.erase(0,1);}
        while(!url.empty()&&std::wstring_view(L".,;!?)］]}").find(url.back())!=std::wstring_view::npos)url.pop_back();
        if(start==std::wstring::npos){if(!std::regex_match(url,bare))continue;url=L"https://"+url;}
        Parse(url);if(!result.empty()&&result!=url)throw std::runtime_error("Summarize one website URL at a time.");result=std::move(url);
    }
    return result;
}
std::wstring WebsiteText(const std::wstring& html){
    auto lower=Lower(html);std::wstring plain;plain.reserve(html.size());
    // Extract text without activating Windows' browser-backed HTML converter.
    // This is a bounded text scan: no scripts, resources, COM objects, or browser lifetime.
    for(size_t i=0;i<html.size();){
        if(lower.compare(i,4,L"<!--")==0){auto end=lower.find(L"-->",i+4);i=end==std::wstring::npos?html.size():end+3;continue;}
        if(html[i]==L'<'){
            size_t nameStart=i+1;bool closing=nameStart<html.size()&&html[nameStart]==L'/';if(closing)++nameStart;
            size_t after=nameStart;while(after<html.size()&&IsTagCharacter(lower[after]))++after;
            if(after>nameStart||(nameStart<html.size()&&(html[nameStart]==L'!'||html[nameStart]==L'?'))){
                auto end=TagEnd(html,after);if(end==std::wstring::npos)break;
                auto tag=lower.substr(nameStart,after-nameStart);
                bool nonReading=tag==L"head"||tag==L"script"||tag==L"style"||tag==L"noscript"||tag==L"svg"||tag==L"template";
                if(!closing&&nonReading&&html[end-1]!=L'/'){
                    std::wstring close=L"</"+tag;size_t found=end+1;
                    while((found=lower.find(close,found))!=std::wstring::npos){
                        size_t boundary=found+close.size();
                        if(boundary==lower.size()||!IsTagCharacter(lower[boundary]))break;
                        found=boundary;
                    }
                    end=found==std::wstring::npos?found:TagEnd(html,found+close.size());
                }
                // Inline markup must not split words; block boundaries separate text.
                if(nonReading||tag==L"p"||tag==L"div"||tag==L"br"||tag==L"hr"||tag==L"li"||tag==L"ul"||tag==L"ol"||tag==L"tr"||tag==L"td"||tag==L"th"||tag==L"section"||tag==L"article"||tag==L"header"||tag==L"footer"||tag==L"nav"||tag==L"main"||tag==L"blockquote"||(tag.size()==2&&tag[0]==L'h'&&tag[1]>=L'1'&&tag[1]<=L'6'))plain+=L' ';
                i=end==std::wstring::npos?html.size():end+1;continue;
            }
        }
        if(html[i]==L'&'){
            auto end=html.find(L';',i+1);
            if(end!=std::wstring::npos&&end-i<=32){auto decoded=DecodeEntity(std::wstring_view(html).substr(i+1,end-i-1));if(!decoded.empty()){plain+=decoded;i=end+1;continue;}}
        }
        plain+=html[i++];
    }
    std::wstring result;bool space=false;
    for(wchar_t c:plain){if(iswspace(c)||c==0xa0){space=!result.empty();continue;}if(space)result+=L' ';space=false;if(c>=32)result+=c;}
    return result;
}
std::wstring ResolveWebsiteUrl(const std::wstring& base,const std::wstring& reference){
    auto ref=Trim(reference);if(ref.empty()||ref.size()>8192||ref.front()==L'#')throw std::runtime_error("Missing website resource URL.");
    wchar_t combined[8192];DWORD length=8192;
    if(FAILED(UrlCombineW(base.c_str(),ref.c_str(),combined,&length,0)))throw std::runtime_error("Unreadable website resource URL.");
    std::wstring result=combined;auto address=Parse(result);return L"https://"+address.host+address.path;
}
WebsiteMetadata InspectWebsite(const std::wstring& html,const std::wstring& url){
    WebsiteMetadata result;auto lower=Lower(html);
    auto addImage=[&](std::wstring reference,std::wstring description,bool logo){
        if(!logo||result.images.size()>=24||reference.empty())return;
        try{
            auto address=reference.starts_with(L"data:image/")?reference:ResolveWebsiteUrl(url,reference);
            if(address.size()>MaxCardAssetChars)return;
            auto existing=std::find_if(result.images.begin(),result.images.end(),[&](const auto& c){return c.url==address;});
            if(existing!=result.images.end()){existing->logo|=logo;return;}
            if(description.empty())description=logo?L"Site logo":L"Page image";
            result.images.push_back({std::move(address),description.substr(0,200),logo});
        }catch(...){}
    };
    for(size_t i=0;(i=html.find(L'<',i))!=std::wstring::npos;){
        if(lower.compare(i,4,L"<!--")==0){auto end=lower.find(L"-->",i+4);i=end==std::wstring::npos?html.size():end+3;continue;}
        size_t begin=i+1,endName=begin;while(endName<html.size()&&IsTagCharacter(lower[endName]))++endName;
        auto end=TagEnd(html,endName);if(end==std::wstring::npos)break;auto tag=lower.substr(begin,endName-begin);auto attrs=ReadAttributes(html,endName,end);i=end+1;
        if(tag==L"script"||tag==L"template"||tag==L"noscript"){auto close=lower.find(L"</"+tag,i);i=close==std::wstring::npos?html.size():close;continue;}
        if(tag==L"style"){
            auto close=lower.find(L"</style",i);if(close==std::wstring::npos)break;
            if(result.css.size()<128*1024)result.css+=html.substr(i,std::min(close-i,128*1024-result.css.size()));i=close;continue;
        }
        if(attrs.contains(L"style")&&result.css.size()<128*1024)result.css+=L"\n"+tag+L"{"+attrs[L"style"].substr(0,2000)+L"}";
        if(tag==L"link"){
            auto rel=Lower(attrs[L"rel"]);
            if(rel==L"stylesheet"&&result.stylesheets.size()<2){try{auto link=ResolveWebsiteUrl(url,attrs[L"href"]);if(std::find(result.stylesheets.begin(),result.stylesheets.end(),link)==result.stylesheets.end())result.stylesheets.push_back(link);}catch(...){} }
            if(rel==L"icon"||rel==L"shortcut icon"||rel==L"apple-touch-icon")addImage(attrs[L"href"],L"Site icon",true);
        }
        if(tag==L"img"){
            auto hints=Lower(attrs[L"alt"]+L" "+attrs[L"class"]+L" "+attrs[L"id"]+L" "+attrs[L"src"]);
            auto ref=attrs[L"src"];if(ref.empty()||ref.starts_with(L"data:")){if(!attrs[L"data-src"].empty())ref=attrs[L"data-src"];}
            if(ref.empty())ref=attrs[L"data-lazy-src"];
            if(ref.empty()){auto set=attrs[L"srcset"];ref=set.substr(0,set.find_first_of(L" ,\t\r\n"));}
            bool tiny=attrs[L"width"]==L"1"||attrs[L"height"]==L"1";
            if(!tiny)addImage(ref,attrs[L"alt"],hints.find(L"logo")!=std::wstring::npos||hints.find(L"brand")!=std::wstring::npos);
        }
        if(tag==L"svg"){
            auto close=lower.find(L"</svg>",i);if(close==std::wstring::npos)continue;
            auto hints=Lower(attrs[L"class"]+L" "+attrs[L"id"]+L" "+attrs[L"aria-label"]);
            if(close+6-begin<150000&&(hints.find(L"logo")!=std::wstring::npos||hints.find(L"brand")!=std::wstring::npos)){
                auto svg=html.substr(begin-1,close+6-(begin-1));if(svg.find(L"xmlns=")==std::wstring::npos)svg.insert(4,L" xmlns=\"http://www.w3.org/2000/svg\"");
                addImage(L"data:image/svg+xml;base64,"+Base64(winrt::to_string(svg)),attrs[L"aria-label"].empty()?L"Site logo":attrs[L"aria-label"],true);
            }i=close+6;
        }
    }
    std::stable_sort(result.images.begin(),result.images.end(),[](const auto& a,const auto& b){auto rank=[](const auto& c){return c.logo?(c.description==L"Site icon"?1:0):2;};return rank(a)<rank(b);});
    return result;
}
std::wstring SummarizeWebsiteStyles(const std::wstring& css){
    // A small sample of declarations, not a claim to reproduce the CSS cascade.
    auto cleaned=css.substr(0,384*1024);
    for(size_t pos=0;(pos=cleaned.find(L"/*",pos))!=std::wstring::npos;){auto end=cleaned.find(L"*/",pos+2);cleaned.erase(pos,end==std::wstring::npos?std::wstring::npos:end+2-pos);}
    std::wstring result;std::set<std::wstring> seen;size_t start=0;
    while(start<cleaned.size()&&result.size()<2200&&seen.size()<28){
        auto open=cleaned.find(L'{',start);if(open==std::wstring::npos)break;auto close=cleaned.find(L'}',open+1);if(close==std::wstring::npos)break;
        auto nested=cleaned.find(L'{',open+1);if(nested<close){start=open+1;continue;}
        auto selector=Trim(cleaned.substr(start,open-start));if(selector.size()>80)selector=selector.substr(0,80);
        std::wstring declarations;
        for(size_t i=open+1;i<close;){auto end=std::min(cleaned.find(L';',i),close);auto colon=cleaned.find(L':',i);
            if(colon<end){auto name=Lower(Trim(cleaned.substr(i,colon-i))),value=Trim(cleaned.substr(colon+1,end-colon-1));auto low=Lower(value);
                bool wanted=name.starts_with(L"--")||name==L"color"||name==L"background"||name==L"background-color"||name==L"font-family"||name==L"font-weight"||name==L"font-size"||name==L"border-radius"||name==L"border"||name==L"letter-spacing"||name==L"box-shadow"||name==L"padding"||name==L"gap";
                if(wanted&&!value.empty()&&value.size()<=160&&low.find(L"url(")==std::wstring::npos&&low.find(L"@import")==std::wstring::npos&&value.find_first_of(L"<>{}")==std::wstring::npos&&seen.insert(name+L":"+value).second)declarations+=name+L": "+value+L"; ";
            }i=end+1;
        }
        if(!declarations.empty())result+=selector+L" { "+declarations+L"}\n";start=close+1;
    }
    if(result.size()>2400)result.resize(2400);return result;
}
namespace {
void RequirePublicHost(const std::wstring& host){
    struct SocketRuntime{SocketRuntime(){WSADATA data;Check(WSAStartup(MAKEWORD(2,2),&data)==0?S_OK:E_FAIL);}~SocketRuntime(){WSACleanup();}} runtime;
    ADDRINFOW hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;ADDRINFOW* addresses=nullptr;
    if(GetAddrInfoW(host.c_str(),L"443",&hints,&addresses))throw std::runtime_error("Could not resolve the website hostname.");
    bool safe=addresses!=nullptr;
    for(auto p=addresses;p;p=p->ai_next){
        if(p->ai_family==AF_INET){auto ip=ntohl(reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr.s_addr);auto a=ip>>24,b=(ip>>16)&255,c=(ip>>8)&255;
            // 192.0.0.0/24 is special-use; public hosts also use the wider 192.0.0.0/16.
            if(a==0||a==10||a==127||a>=224||(a==169&&b==254)||(a==172&&b>=16&&b<=31)||(a==192&&(b==168||(b==0&&c==0)))||(a==100&&b>=64&&b<=127)||(a==198&&(b==18||b==19)))safe=false;
        }else if(p->ai_family==AF_INET6){const auto& ip=reinterpret_cast<sockaddr_in6*>(p->ai_addr)->sin6_addr;if((ip.u.Byte[0]&0xe0)!=0x20)safe=false;}else safe=false;
    }
    FreeAddrInfoW(addresses);if(!safe)throw std::runtime_error("Website resources must use public Internet addresses.");
}
struct Download {std::wstring url,type;std::string bytes;};
Download FetchResource(HINTERNET session,const std::wstring& original,size_t limit,const std::atomic<bool>& canceled,Clock::time_point deadline){
    std::wstring url=original;
    auto check=[&]{if(canceled)throw std::runtime_error("Website reading canceled.");if(Clock::now()>deadline)throw std::runtime_error("The website took too long to read. Retry or paste its text.");};
    for(int redirects=0;redirects<=4;++redirects){
        check();auto address=Parse(url);RequirePublicHost(address.host);
        WebHandle connection(WinHttpConnect(session,address.host.c_str(),443,0));
        WebHandle request(WinHttpOpenRequest(connection.h,L"GET",address.path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;Require(WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)));
        DWORD disabled=WINHTTP_DISABLE_COOKIES;Require(WinHttpSetOption(request.h,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)));
        DWORD logon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;Require(WinHttpSetOption(request.h,WINHTTP_OPTION_AUTOLOGON_POLICY,&logon,sizeof(logon)));
        DWORD decompress=WINHTTP_DECOMPRESSION_FLAG_GZIP|WINHTTP_DECOMPRESSION_FLAG_DEFLATE;Require(WinHttpSetOption(request.h,WINHTTP_OPTION_DECOMPRESSION,&decompress,sizeof(decompress)));
        Require(WinHttpSendRequest(request.h,L"Accept: */*\r\n",static_cast<DWORD>(-1),nullptr,0,0,0));Require(WinHttpReceiveResponse(request.h,nullptr));check();
        DWORD status=0,bytes=sizeof(status);Require(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&bytes,nullptr));
        if(status==301||status==302||status==303||status==307||status==308){
            auto location=Header(request.h,WINHTTP_QUERY_LOCATION);wchar_t combined[8192];DWORD length=8192;
            if(location.empty()||FAILED(UrlCombineW(url.c_str(),location.c_str(),combined,&length,0)))throw std::runtime_error("The website returned an unreadable redirect.");
            url=combined;continue;
        }
        if(status!=200)throw std::runtime_error("The website could not be read (it may require login or block automated access). Paste its text instead.");
        auto type=Lower(Header(request.h,WINHTTP_QUERY_CONTENT_TYPE));
        std::string content;char buffer[16384];DWORD read=0;
        do{check();Require(WinHttpReadData(request.h,buffer,sizeof(buffer),&read));if(content.size()+read>limit)throw std::runtime_error("This website resource is too large.");content.append(buffer,read);}while(read);
        check();return {url,type,std::move(content)};
    }
    throw std::runtime_error("The website redirected too many times.");
}
CardAsset ImportWebsiteImage(const Download& download,const WebsiteImageCandidate& candidate,size_t index){
    CardAsset asset;asset.id=L"site_"+std::to_wstring(index);asset.description=candidate.description;asset.sourceUrl=download.url.starts_with(L"data:")?L"":download.url;
    if(download.type.starts_with(L"image/svg+xml")){
        auto svg=DecodeText(download.bytes,download.type),lower=Lower(svg);
        // SVG is used only as a static image. Reject active content and external dependencies.
        if(svg.size()>150000||lower.find(L"<svg")==std::wstring::npos||lower.find(L"<!")!=std::wstring::npos||lower.find(L"<script")!=std::wstring::npos||lower.find(L"foreignobject")!=std::wstring::npos||lower.find(L"@import")!=std::wstring::npos)
            throw std::runtime_error("This SVG is not a self-contained static image.");
        for(size_t pos=0;(pos=lower.find(L"url(",pos))!=std::wstring::npos;){
            auto end=lower.find(L')',pos+4);if(end==std::wstring::npos)throw std::runtime_error("Invalid SVG resource reference.");
            auto reference=Trim(lower.substr(pos+4,end-pos-4));if(reference.size()>=2&&((reference.front()==L'\''&&reference.back()==L'\'')||(reference.front()==L'"'&&reference.back()==L'"')))reference=reference.substr(1,reference.size()-2);
            if(reference.size()<2||reference.front()!=L'#'||reference.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789_-",1)!=std::wstring::npos)throw std::runtime_error("SVG images must embed their resources.");pos=end+1;
        }
        for(size_t i=0;(i=svg.find(L'<',i))!=std::wstring::npos;){size_t n=i+1;while(n<svg.size()&&IsTagCharacter(lower[n]))++n;auto end=TagEnd(svg,n);if(end==std::wstring::npos)break;auto attrs=ReadAttributes(svg,n,end);
            for(const auto& [name,value]:attrs)if(name.starts_with(L"on")||((name==L"href"||name==L"xlink:href")&&!value.starts_with(L"#")))throw std::runtime_error("This SVG contains active or external content.");
            if(lower.substr(i+1,n-i-1)==L"svg"){
                try{asset.width=static_cast<UINT>(std::clamp(std::stod(attrs[L"width"]),1.,4096.));asset.height=static_cast<UINT>(std::clamp(std::stod(attrs[L"height"]),1.,4096.));}catch(...){
                    std::wistringstream box(attrs[L"viewbox"]);double x,y,w,h;if(box>>x>>y>>w>>h){asset.width=static_cast<UINT>(std::clamp(w,1.,4096.));asset.height=static_cast<UINT>(std::clamp(h,1.,4096.));}
                }
            }i=end+1;
        }
        asset.dataUri=std::make_shared<const std::wstring>(L"data:image/svg+xml;base64,"+Base64(download.bytes));
    }else{
        if(!download.type.starts_with(L"image/"))throw std::runtime_error("The website resource is not an image.");
        std::vector<BYTE> bytes(download.bytes.begin(),download.bytes.end());ImageCodec codec;auto image=codec.Import(bytes,1024);
        if(image->png.size()+32>MaxCardAssetChars)image=codec.Import(bytes,512);
        if(image->width<8||image->height<8)throw std::runtime_error("Skipping tracking pixel or tiny image.");
        asset.width=image->width;asset.height=image->height;asset.dataUri=std::make_shared<const std::wstring>(L"data:image/png;base64,"+image->png);
    }
    ValidateCardAssets({asset});return asset;
}
}
CardAsset DecodeWebsiteImage(const std::string& bytes,const std::wstring& contentType){return ImportWebsiteImage({L"",contentType,bytes},{L"",L"Image",false},1);}
WebsiteSource FetchWebsite(const std::wstring& original,const std::atomic<bool>& canceled){
    WebHandle session(WinHttpOpen(L"Callayers/0.5 WebsiteReader",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0));
    Require(WinHttpSetTimeouts(session.h,5000,5000,5000,10000));const auto deadline=Clock::now()+std::chrono::seconds(60);
    auto page=FetchResource(session.h,original,1024*1024,canceled,deadline);
    bool html=page.type.starts_with(L"text/html")||page.type.starts_with(L"application/xhtml+xml");
    if(!html&&!page.type.starts_with(L"text/plain"))throw std::runtime_error("Use an HTML or text page. PDF and other downloads are not supported yet.");
    auto decoded=DecodeText(page.bytes,page.type);auto text=html?WebsiteText(decoded):decoded;
    WebsiteSource source{page.url,text.substr(0,24000),text.size()>24000};if(!html){if(text.size()<100)throw std::runtime_error("This page has too little readable text.");return source;}
    auto metadata=InspectWebsite(decoded,page.url);int skipped=0;
    for(const auto& link:metadata.stylesheets){
        if(canceled||Clock::now()>deadline)break;
        try{auto css=FetchResource(session.h,link,128*1024,canceled,deadline);if(css.type.starts_with(L"text/css"))metadata.css+=L"\n"+DecodeText(css.bytes,css.type);else ++skipped;}catch(...){++skipped;}
    }
    source.styleSummary=SummarizeWebsiteStyles(metadata.css);
    size_t attempts=0;
    for(const auto& candidate:metadata.images){
        if(canceled||Clock::now()>deadline||!source.assets.empty()||attempts>=8)break;
        if(!candidate.logo)continue;++attempts;
        try{
            Download image;
            if(candidate.url.starts_with(L"data:image/")){
                auto comma=candidate.url.find(L',');if(comma==std::wstring::npos||candidate.url.substr(0,comma).find(L";base64")==std::wstring::npos)throw std::runtime_error("Unsupported inline image.");
                auto data=candidate.url.substr(comma+1);DWORD count=0;Check(CryptStringToBinaryW(data.c_str(),static_cast<DWORD>(data.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,nullptr,&count,nullptr,nullptr)?S_OK:E_FAIL);
                image.bytes.resize(count);Check(CryptStringToBinaryW(data.c_str(),static_cast<DWORD>(data.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,reinterpret_cast<BYTE*>(image.bytes.data()),&count,nullptr,nullptr)?S_OK:E_FAIL);
                image.type=candidate.url.substr(5,candidate.url.find(L';')-5);image.url=candidate.url;
            }else image=FetchResource(session.h,candidate.url,4*1024*1024,canceled,deadline);
            auto asset=ImportWebsiteImage(image,candidate,source.assets.size()+1);
            asset.description=L"Logo: "+asset.description;
            source.assets.push_back(std::move(asset));
        }catch(...){++skipped;}
    }
    if(canceled)throw std::runtime_error("Website reading canceled.");
    if(source.text.size()<100&&source.assets.empty()&&source.styleSummary.empty())throw std::runtime_error("This page has too little readable content; it may require JavaScript. Try another page.");
    if(skipped||Clock::now()>deadline)source.notice=L"Some website logos or styles could not be loaded.";
    return source;
}
}
