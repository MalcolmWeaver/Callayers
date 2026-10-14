#include "generated_card.h"
#include <winrt/base.h>
#include <stdexcept>
#include <iostream>
using namespace callayers;
void check(bool good){if(!good)throw std::runtime_error("Generated card assertion failed");}
int main(){try{winrt::init_apartment();GeneratedCard card;card.id=L"synthetic";card.name=L"Saved \"name\"";card.html=L"<button>+1</button>";card.state=L"{\"score\":2,\"title\":\"abc\"}";
    check(ValidGeneratedCard(card));std::vector<GeneratedCard> restored;check(ParseGeneratedCards(GeneratedCardsJson({card}),restored));check(restored.size()==1 && restored[0].html==card.html && restored[0].state==card.state && !restored[0].visible);
    check(!ValidCardState(L"[]"));check(!ValidCardState(L"{\"__proto__\":{}}"));check(!ValidCardState(L"{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":1}}}}}"));check(!ValidCardState(L"{\"x\":\""+std::wstring(5000,L'x')+L"\"}"));
    check(!ParseGeneratedCards(GeneratedCardsJson({card,card}),restored));
    auto revision=card;revision.html=L"<h1>Edited</h1>";revision.width=600;revision.height=400;card.visible=true;card.layer=2;card.x=100;card.y=100;card.edited=12345;
    GeneratedCard replacement;check(RevisedCard(card,revision,replacement));check(replacement.id==card.id && replacement.name==card.name && replacement.visible && replacement.x==100 && replacement.previous && replacement.state==L"{}" && replacement.layer==2);
    check(MatchesRevisionTarget(card,card.id,card.contentVersion));check(!MatchesRevisionTarget(replacement,card.id,card.contentVersion));check(!MatchesRevisionTarget(card,L"deleted-or-other-card",card.contentVersion));auto renamed=card;++renamed.contentVersion;renamed.name=L"New name";check(!MatchesRevisionTarget(renamed,card.id,card.contentVersion));
    check(ParseGeneratedCards(GeneratedCardsJson({replacement}),restored));check(restored[0].previous && restored[0].previous->html==card.html && restored[0].edited==12345);
    GeneratedCard previous;check(RestorePrevious(restored[0],previous));check(previous.html==card.html && previous.name==card.name && previous.visible && previous.x==100 && !previous.previous && previous.state==L"{}");check(!RestorePrevious(previous,replacement));
    card.width=0;check(!ValidGeneratedCard(card));
    std::cout<<"Generated cards, bounded state, unique identity and roundtrip passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
