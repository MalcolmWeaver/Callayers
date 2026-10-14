#include "annotations.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace callayers {
using namespace winrt::Windows::Data::Json;
static double SegmentDistance(InkPoint p,InkPoint a,InkPoint b) {
    double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
    double t=length?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length,0.0,1.0):0;
    return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
InkBounds Bounds(const Annotation& a) {
    if(a.points.empty())return a.box;
    double left=a.points[0].x,right=left,top=a.points[0].y,bottom=top;
    for(auto p:a.points){left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y);}
    return {left,top,std::max(1.0,right-left),std::max(1.0,bottom-top)};
}
bool HitAnnotation(const Annotation& a,InkPoint p,double tolerance) {
    double pad=tolerance+InkWidths[a.size]/2;
    if(a.kind==AnnotationTool::Pen || a.kind==AnnotationTool::Line || a.kind==AnnotationTool::Arrow) {
        for(size_t i=1;i<a.points.size();++i)if(SegmentDistance(p,a.points[i-1],a.points[i])<=pad)return true;
        if(a.kind==AnnotationTool::Arrow && a.points.size()==2) {
            auto start=a.points[0],end=a.points[1];double angle=std::atan2(end.y-start.y,end.x-start.x),length=16+InkWidths[a.size]*2;
            for(double side:{-0.5,0.5})if(SegmentDistance(p,end,{end.x-length*std::cos(angle+side),end.y-length*std::sin(angle+side)})<=pad)return true;
        }
        return false;
    }
    auto b=Bounds(a);
    if(a.kind==AnnotationTool::Ellipse) {
        double rx=std::max(1.0,b.w/2),ry=std::max(1.0,b.h/2);
        return std::hypot((p.x-b.x-rx)/rx,(p.y-b.y-ry)/ry)<=1+pad/std::min(rx,ry);
    }
    return p.x>=b.x-pad && p.y>=b.y-pad && p.x<=b.x+b.w+pad && p.y<=b.y+b.h+pad;
}
void MoveAnnotation(Annotation& a,const Annotation& original,double dx,double dy) {
    auto b=Bounds(original);dx=std::clamp(dx,-b.x,1280-b.x-b.w);dy=std::clamp(dy,-b.y,720-b.y-b.h);
    a=original;a.box.x+=dx;a.box.y+=dy;
    for(auto& p:a.points){p.x+=dx;p.y+=dy;}
    if(!a.points.empty())a.box=Bounds(a);
}
void ResizeAnnotation(Annotation& a,const Annotation& original,InkPoint corner) {
    a=original;
    if((a.kind==AnnotationTool::Line || a.kind==AnnotationTool::Arrow) && a.points.size()==2){a.points[1]=corner;a.box=Bounds(a);return;}
    auto b=Bounds(original);
    double availableW=std::max(1.0,1280-b.x),availableH=std::max(1.0,720-b.y);
    double w=std::clamp(corner.x-b.x,std::min(8.0,availableW),availableW),h=std::clamp(corner.y-b.y,std::min(8.0,availableH),availableH);
    if(a.kind==AnnotationTool::Text) {
        double factor=std::clamp(std::min(w/b.w,h/b.h),8.0/original.font,600.0/original.font);
        factor=std::min(factor,std::min((1280-b.x)/b.w,(720-b.y)/b.h));
        w=b.w*factor;h=b.h*factor;a.font=original.font*factor;
    }
    a.box={b.x,b.y,w,h};
    for(auto& p:a.points){p.x=b.x+(p.x-b.x)*w/b.w;p.y=b.y+(p.y-b.y)*h/b.h;}
}
// Intersect a stroke segment with the capsule swept by the eraser. The two
// endpoint circles and the strip between them form one convex erased interval.
static bool EraserCut(InkPoint a,InkPoint b,InkPoint start,InkPoint end,double radius,double& lo,double& hi) {
    const double dx=b.x-a.x,dy=b.y-a.y,length=dx*dx+dy*dy;
    lo=1;hi=0;bool hit=false;
    auto interval=[&](double left,double right){left=std::max(0.0,left);right=std::min(1.0,right);if(left<=right){lo=std::min(lo,left);hi=std::max(hi,right);hit=true;}};
    auto circle=[&](InkPoint center){
        double x=a.x-center.x,y=a.y-center.y,c=x*x+y*y-radius*radius;
        if(length<1e-12){if(c<=0)interval(0,1);return;}
        double linear=2*(x*dx+y*dy),discriminant=linear*linear-4*length*c;
        if(discriminant<0)return;double root=std::sqrt(discriminant);
        interval((-linear-root)/(2*length),(-linear+root)/(2*length));
    };
    circle(start);circle(end);
    double sweep=std::hypot(end.x-start.x,end.y-start.y);
    if(sweep>1e-9){
        double ux=(end.x-start.x)/sweep,uy=(end.y-start.y)/sweep,x=a.x-start.x,y=a.y-start.y,left=0,right=1;
        auto clip=[&](double value,double slope,double minimum,double maximum){
            if(std::abs(slope)<1e-12)return value>=minimum && value<=maximum;
            double first=(minimum-value)/slope,last=(maximum-value)/slope;if(first>last)std::swap(first,last);
            left=std::max(left,first);right=std::min(right,last);return left<=right;
        };
        if(clip(x*ux+y*uy,dx*ux+dy*uy,0,sweep) && clip(-x*uy+y*ux,-dx*uy+dy*ux,-radius,radius))interval(left,right);
    }
    return hit && hi-lo>1e-9;
}
PenEraseResult ErasePenPath(std::vector<Annotation>& items,InkPoint start,InkPoint end,double radius,uint64_t& nextId) {
    if(!std::isfinite(radius) || radius<0 || !std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(end.x) || !std::isfinite(end.y))return PenEraseResult::Unchanged;
    std::vector<Annotation> result;result.reserve(items.size());uint64_t candidateId=nextId;size_t pointCount=0;bool changed=false;
    for(const auto& stroke:items){
        std::vector<std::vector<InkPoint>> fragments;std::vector<InkPoint> current;bool cut=false;
        if(stroke.kind==AnnotationTool::Pen && stroke.size>=0 && stroke.size<=2){
            auto append=[&](InkPoint p){if(current.empty() || std::hypot(current.back().x-p.x,current.back().y-p.y)>1e-8)current.push_back(p);};
            auto finish=[&]{if(current.size()>=2)fragments.push_back(std::move(current));current.clear();};
            for(size_t i=1;i<stroke.points.size();++i){
                auto a=stroke.points[i-1],b=stroke.points[i];double lo,hi;
                if(!EraserCut(a,b,start,end,radius+InkWidths[stroke.size]/2,lo,hi)){append(a);append(b);continue;}
                cut=true;auto at=[&](double t){return InkPoint{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};};
                if(lo>1e-9){append(a);append(at(lo));}finish();
                if(hi<1-1e-9){append(at(hi));append(b);}
            }
            finish();
        }
        if(!cut){result.push_back(stroke);pointCount+=stroke.points.size();}
        else {
            changed=true;bool first=true;
            for(auto& points:fragments){
                if(points.size()>4096)return PenEraseResult::LimitReached;
                Annotation fragment=stroke;fragment.points=std::move(points);fragment.box=Bounds(fragment);
                if(!first){if(candidateId>1000000000)return PenEraseResult::LimitReached;fragment.id=candidateId++;}first=false;
                pointCount+=fragment.points.size();result.push_back(std::move(fragment));
                if(result.size()>MaxAnnotations || pointCount>MaxInkPoints)return PenEraseResult::LimitReached;
            }
        }
        if(result.size()>MaxAnnotations || pointCount>MaxInkPoints)return PenEraseResult::LimitReached;
    }
    if(!changed)return PenEraseResult::Unchanged;
    items=std::move(result);nextId=candidateId;return PenEraseResult::Changed;
}


static void Number(JsonObject const& object,const wchar_t* name,double value){object.SetNamedValue(name,JsonValue::CreateNumberValue(value));}
std::wstring AnnotationJson(const std::vector<Annotation>& items) {
    JsonArray array;
    for(const auto& a:items) {
        JsonObject o;Number(o,L"id",double(a.id));Number(o,L"kind",int(a.kind));Number(o,L"size",a.size);Number(o,L"font",a.font);
        auto box=Bounds(a);Number(o,L"x",box.x);Number(o,L"y",box.y);Number(o,L"w",box.w);Number(o,L"h",box.h);
        o.SetNamedValue(L"color",JsonValue::CreateStringValue(a.color));o.SetNamedValue(L"text",JsonValue::CreateStringValue(a.text));
        JsonArray points;for(auto p:a.points){JsonArray pair;pair.Append(JsonValue::CreateNumberValue(p.x));pair.Append(JsonValue::CreateNumberValue(p.y));points.Append(pair);}
        o.SetNamedValue(L"points",points);array.Append(o);
    }
    return std::wstring(array.Stringify());
}
std::vector<Annotation> ParseAnnotations(const std::wstring& json) {
    auto array=JsonArray::Parse(json);std::vector<Annotation> items;size_t pointCount=0;
    if(array.Size()>MaxAnnotations)throw std::runtime_error("Too many annotations");
    auto numeric=[](JsonObject const& o,const wchar_t* name,double minimum,double maximum){double value=o.GetNamedNumber(name);if(!std::isfinite(value)||value<minimum||value>maximum)throw std::runtime_error("Invalid annotation value");return value;};
    for(auto value:array) {
        auto o=value.GetObject();Annotation a;
        double id=numeric(o,L"id",1,1000000000),kind=numeric(o,L"kind",1,6),size=numeric(o,L"size",0,2);
        if(std::floor(id)!=id || std::floor(kind)!=kind || std::floor(size)!=size)throw std::runtime_error("Invalid annotation identity");
        a.id=uint64_t(id);a.kind=AnnotationTool(int(kind));a.size=int(size);a.font=numeric(o,L"font",8,600);
        a.box={numeric(o,L"x",0,1280),numeric(o,L"y",0,720),numeric(o,L"w",0,1280),numeric(o,L"h",0,720)};
        a.color=o.GetNamedString(L"color");a.text=o.GetNamedString(L"text");
        if(a.color.size()!=7 || a.color[0]!=L'#' || !std::all_of(a.color.begin()+1,a.color.end(),[](wchar_t c){return std::iswxdigit(c)!=0;}) || a.text.size()>2000)throw std::runtime_error("Invalid annotation content");
        auto points=o.GetNamedArray(L"points");pointCount+=points.Size();if(pointCount>MaxInkPoints || points.Size()>4096)throw std::runtime_error("Too many points");
        for(auto point:points){auto pair=point.GetArray();if(pair.Size()!=2)throw std::runtime_error("Invalid point");double x=pair.GetNumberAt(0),y=pair.GetNumberAt(1);if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>1280||y<0||y>720)throw std::runtime_error("Invalid point");a.points.push_back({x,y});}
        bool path=a.kind==AnnotationTool::Pen || a.kind==AnnotationTool::Line || a.kind==AnnotationTool::Arrow;
        if((path && a.points.size()<2) || (!path && !a.points.empty()) || ((a.kind==AnnotationTool::Line || a.kind==AnnotationTool::Arrow) && a.points.size()!=2))throw std::runtime_error("Invalid geometry");
        if(!path && (a.box.w<=0 || a.box.h<=0))throw std::runtime_error("Empty geometry");
        auto bounds=Bounds(a);if(bounds.x+bounds.w>1281 || bounds.y+bounds.h>721 || std::any_of(items.begin(),items.end(),[&](const auto& other){return other.id==a.id;}))throw std::runtime_error("Invalid bounds");
        if(a.kind==AnnotationTool::Text && a.text.empty())throw std::runtime_error("Empty text");
        items.push_back(std::move(a));
    }
    return items;
}
}
