#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace callayers {
enum class AnnotationTool { Select, Pen, Rectangle, Ellipse, Line, Arrow, Text, Eraser };
struct InkPoint { double x=0,y=0; };
struct InkBounds { double x=0,y=0,w=1,h=1; };
struct Annotation {
    uint64_t id=0;
    AnnotationTool kind=AnnotationTool::Pen;
    std::wstring color=L"#ffd166";
    int size=1;
    double font=36;
    InkBounds box;
    std::vector<InkPoint> points;
    std::wstring text;
};
inline constexpr double InkWidths[]{3,6,10}, TextSizes[]{24,36,54};
inline constexpr size_t MaxAnnotations=300, MaxInkPoints=65536;
InkBounds Bounds(const Annotation& a);
bool HitAnnotation(const Annotation& a,InkPoint point,double tolerance);
enum class PenEraseResult { Unchanged, Changed, LimitReached };
PenEraseResult ErasePenPath(std::vector<Annotation>& items,InkPoint start,InkPoint end,double radius,uint64_t& nextId);
void MoveAnnotation(Annotation& a,const Annotation& original,double dx,double dy);
void ResizeAnnotation(Annotation& a,const Annotation& original,InkPoint corner);
std::wstring AnnotationJson(const std::vector<Annotation>& items);
std::vector<Annotation> ParseAnnotations(const std::wstring& json);
}
