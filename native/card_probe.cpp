#include "card_host.h"
#include "sample_cards.h"
#include <iostream>
using namespace callayers;
void Require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int LumaAt(const TextOverlay& overlay,UINT x,UINT y){
    for(const auto& p:overlay.luma)if(p.offset==y*Width+x)return p.y;
    return -1;
}
int main(){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;
    try{
        CardHost host;Scene scene;scene.cards={SampleCard(),SampleCard(true)};
        // This sentinel turns white once. Reloading on move/resize turns it black again.
        scene.cards[0].html+=LR"HTML(<div id="sentinel" style="position:absolute;left:40px;top:40px;width:20px;height:20px;background:black"></div><script>setTimeout(()=>document.getElementById('sentinel').style.background='white',1000)</script>)HTML";
        host.SetScene(scene);uint64_t changes=0;size_t previous=0;
        for(int i=0;i<120;++i){
            Sleep(100);Require(SUCCEEDED(host.Error()),"browser/capture failed");auto overlay=host.Latest();
            if(overlay){size_t hash=0;for(const auto& p:overlay->luma)hash=hash*31+p.y+p.alpha;if(previous&&hash!=previous)++changes;previous=hash;}
            if(i==40){scene.cards[0]=MoveCard(scene.cards[0],.03,.04);host.SetScene(scene);}
            if(i==70){scene.cards[0]=ResizeCard(scene.cards[0],2,-.08,.05);host.SetScene(scene);}
            if(i==43||i==73){Require(overlay!=nullptr,"no capture frame");
                Require(LumaAt(*overlay,static_cast<UINT>(scene.cards[0].x*Width)+50,static_cast<UINT>(scene.cards[0].y*Height)+50)>220,"move/resize reloaded the card or geometry was scaled incorrectly");}
        }
        auto overlay=host.Latest();Require(overlay&&changes>50,"animation stopped");
        Require(LumaAt(*overlay,0,0)==-1&&LumaAt(*overlay,1900,1060)==-1,"host background is opaque");
        Require(LumaAt(*overlay,1240,230)>150,"second card style did not remain independent");
        auto background=Pattern(0);auto composed=ComposeText(*background,*overlay);
        Require(composed->pixels[0]==background->pixels[0],"transparent area changed the camera background");
        host.SetActive(false);Sleep(150);auto pausedCount=host.FrameCount();Sleep(250);
        Require(host.FrameCount()==pausedCount,"idle renderer kept capturing frames");
        host.SetActive(true);
        for(int i=0;i<40&&host.FrameCount()==pausedCount;++i)Sleep(50);
        Require(host.FrameCount()>pausedCount&&SUCCEEDED(host.Error()),"renderer did not resume capture");
        Require(LumaAt(*host.Latest(),static_cast<UINT>(scene.cards[0].x*Width)+50,static_cast<UINT>(scene.cards[0].y*Height)+50)>220,"idle/resume recreated the card document");
        std::cout<<"PASS: "<<host.FrameCount()<<" captured frames, "<<changes<<" animation changes; transparent background, separate styles, move/resize without reload\n";
        scene.cards[0].html=L"<style>html,body{margin:0;height:100%;background:transparent}div{height:100%;background:white}</style><div></div>";
        host.SetScene(scene);bool replacement=false;
        for(int i=0;i<30;++i){Sleep(50);auto current=host.Latest();Require(current&&LumaAt(*current,300,300)>=0,"replacement exposed a blank frame");if(LumaAt(*current,300,300)>220){replacement=true;break;}}
        Require(replacement,"prepared replacement did not become visible");
        scene.cards.clear();host.SetScene(scene);Sleep(500);
        auto emptyCount=host.FrameCount();Sleep(250);
        Require(host.FrameCount()==emptyCount,"empty deck kept capturing frames");
        Require(host.Latest()==nullptr,"removed cards left a stale overlay");
        CardElement media;media.id=NewId();media.html=L"<style>body{margin:0}</style><img src='callayers-asset:logo' width='200' height='200'>";
        const std::string svg="<svg xmlns='http://www.w3.org/2000/svg' width='200' height='200'><rect width='200' height='200' fill='white'/></svg>";
        DWORD chars=0;Check(CryptBinaryToStringW(reinterpret_cast<const BYTE*>(svg.data()),static_cast<DWORD>(svg.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&chars)?S_OK:E_FAIL);
        std::wstring base64(chars,L'\0');Check(CryptBinaryToStringW(reinterpret_cast<const BYTE*>(svg.data()),static_cast<DWORD>(svg.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,base64.data(),&chars)?S_OK:E_FAIL);
        base64.resize(chars);while(!base64.empty()&&base64.back()==0)base64.pop_back();CardAsset asset;asset.id=L"logo";asset.description=L"Offline SVG test";asset.width=asset.height=200;asset.dataUri=std::make_shared<const std::wstring>(L"data:image/svg+xml;base64,"+base64);media.assets={asset};scene.cards={media};
        scene=DecodeScene(EncodeScene(scene));host.SetScene(scene);bool imageVisible=false;
        for(int i=0;i<40;++i){Sleep(50);auto current=host.Latest();if(current&&LumaAt(*current,250,220)>220){imageVisible=true;break;}}
        Require(imageVisible,"saved card image did not render offline under the card policy");
        std::cout<<"PASS: embedded SVG asset survives save/reopen and renders through the offline card host\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";result=1;}
    CoUninitialize();return result;
}
