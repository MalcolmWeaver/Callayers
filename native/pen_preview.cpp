#include "pen_preview.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cwchar>
#include <stdexcept>

namespace callayers {
PenPreview::PenPreview() {
    Gdiplus::GdiplusStartupInput input;
    if(Gdiplus::GdiplusStartup(&graphicsToken_,&input,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Couldn't initialize pen preview");
}
PenPreview::~PenPreview(){if(graphicsToken_)Gdiplus::GdiplusShutdown(graphicsToken_);}
void PenPreview::follow(uint64_t id,uint64_t revision) {
    auto stroke=std::find_if(pending_.begin(),pending_.end(),[id](const auto& item){return item.id==id;});
    if(stroke==pending_.end())pending_.push_back({id,revision});else stroke->revision=revision;
}
void PenPreview::cancel(uint64_t id) {
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),[id](const auto& item){return item.id==id;}),pending_.end());
}
bool PenPreview::paint(HDC dc,const std::vector<Annotation>& annotations,const RECT& preview,uint64_t renderedRevision) {
    if(pending_.empty() || preview.right<=preview.left || preview.bottom<=preview.top)return false;
    const double sx=double(preview.right-preview.left)/1280,sy=double(preview.bottom-preview.top)/720;
    Gdiplus::Graphics graphics(dc);
    graphics.SetClip(Gdiplus::Rect(preview.left,preview.top,preview.right-preview.left,preview.bottom-preview.top));
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    bool painted=false;
    for(auto pending=pending_.begin();pending!=pending_.end();){
        auto stroke=std::find_if(annotations.begin(),annotations.end(),[id=pending->id](const auto& a){return a.id==id;});
        if(renderedRevision>=pending->revision || stroke==annotations.end() || stroke->kind!=AnnotationTool::Pen){pending=pending_.erase(pending);continue;}
        if(stroke->points.size()>=2 && stroke->size>=0 && stroke->size<=2 && stroke->color.size()==7){
            std::vector<Gdiplus::PointF> points;points.reserve(stroke->points.size());
            for(auto p:stroke->points)points.emplace_back(float(preview.left+p.x*sx),float(preview.top+p.y*sy));
            auto rgb=std::wcstoul(stroke->color.c_str()+1,nullptr,16);
            Gdiplus::Pen pen(Gdiplus::Color(255,BYTE(rgb>>16),BYTE(rgb>>8),BYTE(rgb)),float(InkWidths[stroke->size]*std::min(sx,sy)));
            pen.SetLineCap(Gdiplus::LineCapRound,Gdiplus::LineCapRound,Gdiplus::DashCapRound);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            if(graphics.DrawLines(&pen,points.data(),INT(points.size()))==Gdiplus::Ok)painted=true;
        }
        ++pending;
    }
    return painted;
}
}
