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
        std::cout<<"PASS: "<<host.FrameCount()<<" captured frames, "<<changes<<" animation changes; transparent background, separate styles, move/resize without reload\n";
        scene.cards[0].html=L"<style>html,body{margin:0;height:100%;background:transparent}div{height:100%;background:white}</style><div></div>";
        host.SetScene(scene);bool replacement=false;
        for(int i=0;i<30;++i){Sleep(50);auto current=host.Latest();Require(current&&LumaAt(*current,300,300)>=0,"replacement exposed a blank frame");if(LumaAt(*current,300,300)>220){replacement=true;break;}}
        Require(replacement,"prepared replacement did not become visible");
        scene.cards.clear();host.SetScene(scene);Sleep(500);overlay=host.Latest();Require(overlay&&overlay->luma.empty(),"removed cards left stale pixels");
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";result=1;}
    CoUninitialize();return result;
}
