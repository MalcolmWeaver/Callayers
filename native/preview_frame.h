#pragma once
#include "capture.h"
#include "frame_bridge.h"
#include "resource_diagnostics.h"

namespace callayers {
struct PresentedFrame {
    std::vector<uint8_t> pixels;
    bool live=false;
    ULONGLONG tick=0,cameraTick=0;
    unsigned version=0;
    uint64_t hiddenTextId=0,sceneRevision=0;
};
inline bool ShouldReplacePreview(const PresentedFrame* current,const PresentedFrame& next) {
    if(!current)return true;
    if(next.version!=current->version)return next.version>current->version;
    if(next.sceneRevision!=current->sceneRevision)return next.sceneRevision>current->sceneRevision;
    if(next.tick!=current->tick)return next.tick>current->tick;
    return next.cameraTick>=current->cameraTick;
}

template<class OverlayFrame>
void ComposeFrame(const CameraImage& camera,const OverlayFrame& overlay,std::vector<uint8_t>& output) {
    StageTimer timing(VideoStage::Compose);
    output.assign(PixelBytes,0);
    double scale=std::min(double(Width)/camera.width,double(Height)/camera.height);
    int w=std::max(1,int(camera.width*scale)),h=std::max(1,int(camera.height*scale));
    int left=(Width-w)/2,top=(Height-h)/2;
    for(int y=0;y<h;++y){
        UINT sourceY=std::min(camera.height-1,UINT(y/scale));
        auto dst=output.data()+(size_t(y+top)*Width+left)*4;
        auto row=camera.pixels.data()+size_t(sourceY)*camera.width*4;
        if(w==int(camera.width))memcpy(dst,row,size_t(w)*4);
        else for(int x=0;x<w;++x){UINT sourceX=std::min(camera.width-1,UINT(x/scale));memcpy(dst+x*4,row+sourceX*4,4);}
    }
    for(size_t i=0;i<PixelBytes;i+=4){
        unsigned inverse=255-overlay.pixels[i+3];
        for(int c=0;c<3;++c)output[i+c]=uint8_t(std::min(255u,unsigned(overlay.pixels[i+c])+(unsigned(output[i+c])*inverse+127)/255));
        output[i+3]=255;
    }
}
}
