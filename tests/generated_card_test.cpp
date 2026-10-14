#include "generated_card.h"
#include <winrt/base.h>
#include <stdexcept>
#include <iostream>
using namespace callayers;
void check(bool good){if(!good)throw std::runtime_error("Generated card assertion failed");}
int main(){try{winrt::init_apartment();GeneratedCard card;card.id=L"synthetic";card.name=L"Saved \"name\"";card.html=L"<button>+1</button>";card.state=L"{\"score\":2,\"title\":\"abc\"}";
    check(ValidGeneratedCard(card));std::vector<GeneratedCard> restored;check(ParseGeneratedCards(GeneratedCardsJson({card}),restored));check(restored.size()==1 && restored[0].html==card.html && restored[0].state==card.state && !restored[0].visible);
    check(!ValidCardState(L"[]"));check(!ValidCardState(L"{\"__proto__\":{}}"));check(!ValidCardState(L"{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":1}}}}}"));check(!ValidCardState(L"{\"x\":\""+std::wstring(5000,L'x')+L"\"}"));
    check(!ParseGeneratedCards(GeneratedCardsJson({card,card}),restored));card.width=0;check(!ValidGeneratedCard(card));
    std::cout<<"Generated cards, bounded state, unique identity and roundtrip passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
