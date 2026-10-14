#include "annotations.h"
#include "frame_bridge.h"
#include "overlay_ultralight.h"
#include "pen_preview.h"
#include "preview_frame.h"
#include <winrt/base.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace callayers;
static void Check(bool condition,const char* reason) {if(!condition)throw std::runtime_error(reason);}
static std::shared_ptr<OverlayImage> Image(Overlay& overlay) {
    std::lock_guard lock(overlay.state->mutex);return overlay.state->image;
}
static bool RedAt(const OverlayImage& image,int x,int y) {
    auto offset=(size_t(y)*Width+x)*4;
    return image.pixels[offset+2]>200 && image.pixels[offset+1]<30 && image.pixels[offset]<30 && image.pixels[offset+3]>200;
}
static std::wstring Scene(const std::vector<Annotation>& annotations) {
    return L"{\"cards\":[],\"annotations\":"+AnnotationJson(annotations)+L"}";
}
static void LocalPreview() {
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-Height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr);void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    Check(dc && bitmap && pixels,"Live preview test surface must initialize");
    auto old=SelectObject(dc,bitmap);
    try {
        PenPreview preview;RECT area{100,80,740,440};
        Annotation stroke;stroke.id=1;stroke.kind=AnnotationTool::Pen;stroke.color=L"#ff0000";stroke.size=1;stroke.points={{120,120},{200,120}};
        auto clear=[&]{std::memset(pixels,0,PixelBytes);};
        auto red=[&](int x,int y){GdiFlush();auto p=static_cast<uint8_t*>(pixels)+(size_t(y)*Width+x)*4;return p[2]>180 && p[1]<30 && p[0]<30;};
        clear();preview.follow(stroke.id,1);
        Check(preview.paint(dc,{stroke},area,0) && red(180,140),"Local ink must appear before the compositor supplies a new frame");
        stroke.points.push_back({320,120});preview.follow(stroke.id,2);clear();
        Check(preview.paint(dc,{stroke},area,1) && red(230,140),"New pen segments must appear while the rendered frame still has the shorter stroke");
        Check(!red(80,140),"Local ink must stay within the preview");
        clear();Check(!preview.paint(dc,{stroke},area,2) && !red(230,140),"Native ink must hand over once the composed frame catches up");
        preview.follow(stroke.id,3);clear();Check(preview.paint(dc,{stroke},area,2) && red(230,140),"Pending ink must remain visible after release until the frame catches up");
        preview.clear();clear();Check(!preview.paint(dc,{stroke},area,2) && !red(230,140),"Cancelling a stroke must clear the immediate preview");
        stroke.points={{0,120},{1280,120}};preview.follow(stroke.id,4);clear();
        Check(preview.paint(dc,{stroke},area,3) && red(101,140) && !red(99,140) && !red(740,140),"Even thick edge strokes must not paint outside the camera preview");
        preview.clear();
        Annotation firstStroke=stroke;firstStroke.id=1;firstStroke.points={{120,120},{200,120}};
        Annotation secondStroke=firstStroke;secondStroke.id=2;secondStroke.points={{120,200},{200,200}};
        preview.follow(firstStroke.id,5);preview.follow(secondStroke.id,6);clear();
        Check(preview.paint(dc,{firstStroke,secondStroke},area,4) && red(180,140) && red(180,180),"Starting a second stroke must keep both uncomposited strokes visible");
        clear();Check(preview.paint(dc,{firstStroke,secondStroke},area,5) && !red(180,140) && red(180,180),"Each pending stroke must hand over only when its own composed revision arrives");

        preview.follow(firstStroke.id,7);preview.follow(secondStroke.id,8);preview.cancel(secondStroke.id);clear();
        Check(preview.paint(dc,{firstStroke},area,6) && red(180,140) && !red(180,180),"Cancelling the active stroke must preserve older strokes still waiting for composition");
    } catch(...) {SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);throw;}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
static void PreviewOrdering() {
    PresentedFrame immediate,worker;immediate.version=7;immediate.sceneRevision=40;immediate.tick=100;
    worker=immediate;worker.sceneRevision=39;worker.tick=101;
    Check(!ShouldReplacePreview(&immediate,worker),"An older card position must not overwrite an immediate drag preview, even with a newer camera timestamp");
    worker.sceneRevision=41;Check(ShouldReplacePreview(&immediate,worker),"A completed frame at the latest card position must replace the drag preview");
    worker=immediate;worker.version=6;worker.sceneRevision=100;
    Check(!ShouldReplacePreview(&immediate,worker),"A frame from an older camera session must not replace the current preview");
    worker=immediate;worker.tick=99;
    Check(!ShouldReplacePreview(&immediate,worker),"A stale frame at the same card position must not replace a fresher preview");
    immediate.cameraTick=100;worker=immediate;worker.cameraTick=99;
    Check(!ShouldReplacePreview(&immediate,worker),"A delayed camera image must not freeze the preview at an unchanged card position" );
    worker.cameraTick=101;Check(ShouldReplacePreview(&immediate,worker),"New camera images must keep the preview live while a card is held in place" );
    worker=immediate;worker.version=8;worker.sceneRevision=1;
    Check(ShouldReplacePreview(&immediate,worker) && ShouldReplacePreview(nullptr,worker),"A new camera session and the first preview must be accepted");
}
static void CardPreview(Overlay& overlay) {
    auto scene=[](int x,int y){return L"{\"cards\":[{\"id\":0,\"x\":"+std::to_wstring(x)+L",\"y\":"+std::to_wstring(y)+L",\"w\":300,\"h\":160,\"contentW\":480,\"contentH\":128,\"visible\":true}],\"annotations\":[]}";};
    overlay.update(scene(80,80));
    bool ready=false;
    for(int i=0;i<100 && !ready;++i){overlay.capture();auto frame=Image(overlay);ready=frame && frame->pixels[(size_t(120)*Width+200)*4+3]>100;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    Check(ready,"The synthetic card must load before drag preview checks");
    overlay.update(scene(620,360));overlay.capture();auto moved=Image(overlay);
    Check(moved && moved->sceneRevision==overlay.sceneRevision(),"A drag render must represent the latest card position");
    Check(moved->pixels[(size_t(120)*Width+200)*4+3]==0 && moved->pixels[(size_t(400)*Width+740)*4+3]>100,"The card must move to the current pointer position without a second renderer tick or a ghost at the old position");
    CameraImage camera;camera.width=Width;camera.height=Height;camera.pixels.resize(PixelBytes);
    for(size_t i=0;i<PixelBytes;i+=4){camera.pixels[i]=20;camera.pixels[i+1]=30;camera.pixels[i+2]=40;camera.pixels[i+3]=255;}
    std::vector<uint8_t> preview;ComposeFrame(camera,*moved,preview);
    auto old=(size_t(120)*Width+200)*4,newAt=(size_t(400)*Width+740)*4;
    Check(preview[old]==20 && preview[old+1]==30 && preview[old+2]==40,"The immediate composition must restore the camera at the old card location");
    Check(preview[newAt]!=20 || preview[newAt+1]!=30 || preview[newAt+2]!=40,"The immediate composition must include the card at its latest location");
}

int main() {
    winrt::init_apartment();
    try {
        PreviewOrdering();
        LocalPreview();
        Overlay overlay;overlay.update(Scene({}));overlay.initialize(GetModuleHandleW(nullptr));
        for(int i=0;i<100 && !Image(overlay);++i) {
            overlay.capture();std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(overlay.state->ready && !overlay.state->failed && Image(overlay),"Overlay must initialize before live ink checks");
        Annotation stroke;stroke.id=1;stroke.kind=AnnotationTool::Pen;stroke.color=L"#ff0000";stroke.size=1;stroke.points={{120,120},{121,120}};
        for(int step=1;step<=8;++step) {
            int endpoint=120+step*40;stroke.points.push_back({double(endpoint),120});
            overlay.update(Scene({stroke}));overlay.capture();
            auto frame=Image(overlay);
            Check(frame && !overlay.state->failed,"Every drawing update must publish a frame");
            Check(frame->sceneRevision==overlay.sceneRevision(),"The composed image must identify the scene it actually rendered");
            Check(RedAt(*frame,endpoint-20,120),"The latest pen segment must be visible during drawing, before release");
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        overlay.update(Scene({}));overlay.capture();
        auto cancelled=Image(overlay);
        Check(cancelled && !RedAt(*cancelled,140,120),"Cancelling the current stroke must remove its live ink");
        CardPreview(overlay);
        std::cout<<"Live pen, partial preview handover, immediate card composition and frame ordering passed.\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    catch(...) {std::cerr<<"Live ink rendering test failed.\n";return 1;}
}
