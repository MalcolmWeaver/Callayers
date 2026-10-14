#include "card_state.h"
#include "card_input.h"
#include <winrt/base.h>
#include <iostream>
#include <stdexcept>
using namespace callayers;
void require(bool ok){if(!ok)throw std::runtime_error("Interactive card assertion failed");}
int main(){try{winrt::init_apartment();InteractiveState value;
    require(ParseInteractiveState(L"{\"count\":3,\"title\":\"Hello\",\"remaining\":42.5}",value));
    require(value.count==3 && value.title==L"Hello" && value.remaining==42.5);
    InteractiveState restored;value.title=L"Quotes \" and newline\n";
    require(ParseInteractiveState(InteractiveStateJson(value),restored));require(restored.title==value.title);
    for(auto bad:{L"{}",L"{\"count\":-1,\"title\":\"x\",\"remaining\":10}",L"{\"count\":1.2,\"title\":\"x\",\"remaining\":10}",L"{\"count\":1,\"title\":\"x\",\"remaining\":301}",L"{\"count\":1,\"title\":\"x\",\"remaining\":10,\"extra\":true}"})require(!ParseInteractiveState(bad,restored));
    require(HitCardPart(10,20,100,80,60,60,5)==CardPart::Inside);
    require(HitCardPart(10,20,100,80,10,60,5)==CardPart::Border);
    require(HitCardPart(10,20,100,80,10,20,5)==CardPart::TopLeft);
    require(HitCardPart(10,20,100,80,110,20,5)==CardPart::TopRight);
    require(HitCardPart(10,20,100,80,10,100,5)==CardPart::BottomLeft);
    require(HitCardPart(10,20,100,80,110,100,5)==CardPart::BottomRight);
    require(HitCardPart(10,20,100,80,0,60,5)==CardPart::None);
    std::cout<<"Card schema, round trip and border/corner targeting passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
