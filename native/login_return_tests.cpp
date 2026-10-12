#include "login_return.h"
#include <iostream>
#include <thread>

int main(){
    try{
        const std::string state(64,'a'),token="refresh_token.with/special+characters=";
        callayers::LoginReturn listener(state,"https://callayers.example");
        auto request=[&](const std::string& method,const std::string& suppliedState,const std::string& origin,const std::string& body,bool valid,bool fragmented=false){
            SOCKET client=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(client==INVALID_SOCKET)throw std::runtime_error("socket failed");
            struct Close { SOCKET value;~Close(){closesocket(value);} } close{client};
            DWORD timeout=2000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(listener.port);
            if(connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address)))throw std::runtime_error("connect failed: "+std::to_string(WSAGetLastError()));
            auto text=method+" /callback?state="+suppliedState+" HTTP/1.1\r\nHost: 127.0.0.1:"+std::to_string(listener.port)+
                "\r\nOrigin: "+origin+"\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
            std::thread sender([&]{
                if(fragmented){auto split=text.size()-8;send(client,text.data(),static_cast<int>(split),0);Sleep(30);send(client,text.data()+split,8,0);}
                else send(client,text.data(),static_cast<int>(text.size()),0);
            });
            auto received=listener.Poll();sender.join();char response[2048];int n=recv(client,response,sizeof(response),0);
            if(n<=0||std::string(response,n).find(valid?"200 OK":"400 Bad Request")==std::string::npos||received!=(valid?token:std::string{}))
                throw std::runtime_error("callback validation failed");
        };
        const auto body="refresh_token=refresh_token.with%2Fspecial%2Bcharacters%3D";
        request("POST",std::string(64,'c'),"https://callayers.example",body,false);
        request("GET",state,"https://callayers.example",body,false);
        request("POST",state,"https://attacker.example",body,false);
        request("POST",state,"null",body,false);
        request("POST",state,"https://callayers.example","refresh_token=x%0D%0A",false);
        request("POST",state,"https://callayers.example","refresh_token=x%ZZ",false);
        request("POST",state,"https://callayers.example","refresh_token=x&refresh_token=y",false);
        request("POST",state,"https://callayers.example",body,true);
        request("POST",state,"https://callayers.example",body,true,true);
        std::cout<<"Loopback POST validation and fragmented credential transfer passed.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
