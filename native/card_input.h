#pragma once
#include <algorithm>
#include <cmath>
namespace callayers {
enum class CardPart {None,Inside,Border,TopLeft,TopRight,BottomLeft,BottomRight};
inline CardPart HitCardPart(double x,double y,double w,double h,double px,double py,double tolerance){
    if(px<x-tolerance || px>x+w+tolerance || py<y-tolerance || py>y+h+tolerance)return CardPart::None;
    bool left=std::abs(px-x)<=tolerance,right=std::abs(px-x-w)<=tolerance;
    bool top=std::abs(py-y)<=tolerance,bottom=std::abs(py-y-h)<=tolerance;
    if(left && top)return CardPart::TopLeft;if(right && top)return CardPart::TopRight;
    if(left && bottom)return CardPart::BottomLeft;if(right && bottom)return CardPart::BottomRight;
    if(left||right||top||bottom)return CardPart::Border;
    return CardPart::Inside;
}
}
