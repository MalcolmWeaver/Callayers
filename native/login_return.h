#pragma once
// Include before windows.h so Winsock 1 declarations are not pulled in.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <stdexcept>
#include <utility>
#include <map>
#include <algorithm>
#include <charconv>

namespace callayers {
// One sign-in attempt owns one ephemeral loopback port. No LAN interface,
// registered URL scheme, firewall exception, or installed helper is needed.
class LoginReturn {
    struct Socket {
        SOCKET value=INVALID_SOCKET;
        ~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}
    };
    struct Winsock {
        Winsock(){WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))throw std::runtime_error("Could not start browser sign-in.");}
        ~Winsock(){WSACleanup();}
    } winsock;
    Socket listener;
    std::string state, origin;
public:
    unsigned short port=0;
    explicit LoginReturn(std::string value,std::string expectedOrigin):state(std::move(value)),origin(std::move(expectedOrigin)){
        listener.value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        BOOL exclusive=TRUE;
        if(listener.value==INVALID_SOCKET||setsockopt(listener.value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive)))
            throw std::runtime_error("Could not reserve the browser return address.");
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        int size=sizeof(address);
        if(bind(listener.value,reinterpret_cast<sockaddr*>(&address),size)||listen(listener.value,4)||getsockname(listener.value,reinterpret_cast<sockaddr*>(&address),&size))
            throw std::runtime_error("Could not open the browser return address.");
        port=ntohs(address.sin_port);
    }
    std::string Poll(){
        fd_set ready;FD_ZERO(&ready);FD_SET(listener.value,&ready);timeval timeout{0,250000};
        if(select(0,&ready,nullptr,nullptr,&timeout)<=0)return {};
        Socket client{accept(listener.value,nullptr,nullptr)};if(client.value==INVALID_SOCKET)return {};
        DWORD milliseconds=250;setsockopt(client.value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&milliseconds),sizeof(milliseconds));
        setsockopt(client.value,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&milliseconds),sizeof(milliseconds));
        std::string request;char bytes[2048];size_t end=std::string::npos,length=0;
        std::map<std::string,std::string> headers;bool valid=true;
        while(request.size()<16384){
            int n=recv(client.value,bytes,sizeof(bytes),0);if(n<=0)return {};request.append(bytes,n);
            if(end==std::string::npos){
                end=request.find("\r\n\r\n");if(end==std::string::npos)continue;
                auto line=request.find("\r\n");
                valid=request.substr(0,line)=="POST /callback?state="+state+" HTTP/1.1";
                for(auto start=line+2;start<end;){
                    auto next=request.find("\r\n",start),colon=request.find(':',start);
                    if(colon>=next){valid=false;break;}
                    auto name=request.substr(start,colon-start),value=request.substr(colon+1,next-colon-1);
                    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return c>='A'&&c<='Z'?char(c+32):char(c);});
                    while(!value.empty()&&(value.front()==' '||value.front()=='\t'))value.erase(0,1);
                    while(!value.empty()&&(value.back()==' '||value.back()=='\t'))value.pop_back();
                    if(!headers.emplace(name,value).second)valid=false;
                    start=next+2;
                }
                const auto& size=headers["content-length"];
                const auto parsed=std::from_chars(size.data(),size.data()+size.size(),length);
                valid=valid&&parsed.ec==std::errc{}&&parsed.ptr==size.data()+size.size()&&length>14&&length<=8192&&
                    !headers.contains("transfer-encoding")&&headers["origin"]==origin&&
                    headers["host"]=="127.0.0.1:"+std::to_string(port)&&
                    headers["content-type"]=="application/x-www-form-urlencoded";
                if(!valid)break;
            }
            if(request.size()>=end+4+length)break;
        }
        std::string grant;
        if(valid&&end!=std::string::npos&&request.size()==end+4+length){
            const auto data=request.substr(end+4);
            if(data.starts_with("refresh_token=")&&data.find('&')==std::string::npos){
                const auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
                for(size_t i=14;i<data.size();++i){
                    unsigned char c=data[i];
                    if(c=='%'){
                        if(i+2>=data.size()||hex(data[i+1])<0||hex(data[i+2])<0){valid=false;break;}
                        c=static_cast<unsigned char>((hex(data[i+1])<<4)|hex(data[i+2]));i+=2;
                    }else if(c=='+')c=' ';
                    if(c<=32||c>=127){valid=false;break;}grant+=char(c);
                }
                if(!valid||grant.size()>2560)grant.clear();
            }
        }
        const std::string body=grant.empty()?"Invalid sign-in return. Start sign-in again in Callayers.":
            "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Return to Callayers</title><style>body{margin:0;background:#19191e;color:#eeeaf7;font:16px/1.6 system-ui,sans-serif}main{max-width:540px;margin:12vh auto;padding:32px}.brand{color:#b2aafa;letter-spacing:.2em;font-weight:700}h1{line-height:1.2}p{color:#bbb6c9}</style></head><body><main><p class=\"brand\">CALLAYERS</p><h1>Returning to Callayers</h1><p>Finish in the Callayers app. You can close this tab.</p><p>If the app is behind your browser, select Callayers in the taskbar.</p></main></body></html>";
        const auto response=std::string(grant.empty()?"HTTP/1.1 400 Bad Request\r\n":"HTTP/1.1 200 OK\r\n")+
            "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'\r\nConnection: close\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
        send(client.value,response.data(),static_cast<int>(response.size()),0);
        return grant;
    }
};
}
