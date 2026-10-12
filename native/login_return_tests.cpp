#include "login_return.h"
#include <iostream>

int main(){
    try{
        const std::string state(64,'a'),grant(64,'b');callayers::LoginReturn listener(state);
        auto request=[&](const std::string& method,const std::string& suppliedState,const std::string& suppliedGrant,bool valid){
            SOCKET client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(client==INVALID_SOCKET)throw std::runtime_error("socket failed");
            struct Close { SOCKET value;~Close(){closesocket(value);} } close{client};
            DWORD timeout=2000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(listener.port);
            if(connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address)))throw std::runtime_error("connect failed: "+std::to_string(WSAGetLastError()));
            auto text=method+" /callback?state="+suppliedState+"&grant="+suppliedGrant+" HTTP/1.1\r\nHost: 127.0.0.1:"+std::to_string(listener.port)+"\r\n\r\n";
            if(send(client,text.data(),static_cast<int>(text.size()),0)!=static_cast<int>(text.size()))throw std::runtime_error("send failed");
            auto received=listener.Poll();char response[2048];int n=recv(client,response,sizeof(response),0);
            if(n<=0||std::string(response,n).find(valid?"200 OK":"400 Bad Request")==std::string::npos||received!=(valid?grant:std::string{}))
                throw std::runtime_error("callback validation failed");
        };
        request("GET",std::string(64,'c'),grant,false);
        request("GET",state,"short",false);
        request("POST",state,grant,false);
        request("GET",state,grant,true);
        std::cout<<"Loopback callback accepts the matching GET and rejects wrong state, grant shape, and method.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
