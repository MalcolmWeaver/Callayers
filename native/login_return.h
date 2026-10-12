#pragma once
// Include before windows.h so Winsock 1 declarations are not pulled in.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <stdexcept>
#include <utility>

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
    std::string state;
public:
    unsigned short port=0;
    explicit LoginReturn(std::string value):state(std::move(value)){
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
        std::string request;char bytes[2048];
        for(int i=0;i<8&&request.find("\r\n\r\n")==std::string::npos;++i){int n=recv(client.value,bytes,sizeof(bytes),0);if(n<=0)return {};request.append(bytes,n);}
        const auto prefix="GET /callback?state="+state+"&grant=";
        std::string grant;
        if(request.find("\r\n\r\n")!=std::string::npos&&request.starts_with(prefix)&&request.size()>=prefix.size()+64+11&&request.substr(prefix.size()+64,11)==" HTTP/1.1\r\n"){
            auto candidate=request.substr(prefix.size(),64);
            if(candidate.find_first_not_of("0123456789abcdef")==std::string::npos)grant=std::move(candidate);
        }
        const std::string body=grant.empty()?"Invalid sign-in return. Start sign-in again in Callayers.":
            "<!doctype html><html><head><meta charset=\"utf-8\"><title>Callayers</title></head><body><h1>Returning to Callayers</h1><p>You can close this tab. If Windows keeps the browser in front, select Callayers in the taskbar.</p></body></html>";
        const auto response=std::string(grant.empty()?"HTTP/1.1 400 Bad Request\r\n":"HTTP/1.1 200 OK\r\n")+
            "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'; frame-ancestors 'none'\r\nConnection: close\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
        send(client.value,response.data(),static_cast<int>(response.size()),0);
        return grant;
    }
};
}
