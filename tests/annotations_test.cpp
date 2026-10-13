#include "annotations.h"
#include <winrt/base.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace callayers;
static void Check(bool condition,const char* reason) {if(!condition)throw std::runtime_error(reason);}
static bool Near(double a,double b) {return std::abs(a-b)<0.001;}
static void Reject(const std::vector<Annotation>& items) {
    bool rejected=false;try{ParseAnnotations(AnnotationJson(items));}catch(...){rejected=true;}
    Check(rejected,"Invalid saved annotations must be rejected");
}
int main() {
    winrt::init_apartment();
    try {
        std::vector<Annotation> items;
        for(int kind=1;kind<=6;++kind) {
            Annotation a;a.id=kind;a.kind=AnnotationTool(kind);a.color=L"#00bfff";a.box={100,80,240,90};
            if(kind==1 || kind==4 || kind==5)a.points={{120,90},{330,170}};
            if(kind==6)a.text=L"<Hello> & \"world\"\nCafé 世界";
            items.push_back(a);
        }
        auto restored=ParseAnnotations(AnnotationJson(items));
        Check(restored.size()==6 && restored[5].text==items[5].text,"Text must restore literally, including escapes and Unicode");
        for(size_t i=0;i<items.size();++i)Check(restored[i].kind==items[i].kind && restored[i].id==items[i].id && restored[i].color==items[i].color && restored[i].size==items[i].size,"All annotation types and styles must restore");
        Check(restored[4].points.size()==2 && Near(restored[4].points[1].x,330),"Path points must restore");

        Annotation moved;MoveAnnotation(moved,items[0],5000,-5000);auto b=Bounds(moved);
        Check(Near(b.x+b.w,1280) && Near(b.y,0),"Moving a stroke must keep it within the picture");
        Check(Near(moved.points[1].x-moved.points[0].x,210),"Moving must preserve stroke geometry");
        Annotation resized;ResizeAnnotation(resized,items[5],{580,260});
        Check(Near(resized.box.w,480) && Near(resized.box.h,180) && Near(resized.font,72),"Text must resize proportionally with its font");
        Annotation arrow=items[4];arrow.points={{330,90},{120,170}};
        ResizeAnnotation(resized,arrow,{700,600});
        Check(Near(resized.points[0].x,330) && Near(resized.points[1].x,700) && Near(resized.points[1].y,600),"Arrow resize must move its endpoint, including reversed arrows");
        Check(HitAnnotation(arrow,{225,130},3) && !HitAnnotation(arrow,{225,20},3),"Path selection must follow the visible line");
        ParseAnnotations(AnnotationJson({moved,resized}));

        auto invalid=items;invalid[1].id=invalid[0].id;Reject(invalid);
        invalid=items;invalid[0].points[0].x=-1;Reject(invalid);
        invalid=items;invalid[1].box.w=0;Reject(invalid);
        invalid=items;invalid[4].points.push_back({200,200});Reject(invalid);
        invalid=items;invalid[5].color=L"red";Reject(invalid);
        invalid=items;invalid[5].text=std::wstring(2001,L'x');Reject(invalid);
        std::cout<<"Annotation restoration, geometry, hit testing and invalid data checks passed.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    catch(...){std::cerr<<"Annotation test failed.\n";return 1;}
}
