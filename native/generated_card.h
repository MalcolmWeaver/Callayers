#pragma once
#include <string>
#include <vector>
#include <optional>
namespace callayers {
#ifdef CALLAYERS_LIBRARY
inline constexpr int SavedCardOffset=0;
#else
inline constexpr int SavedCardOffset=3;
#endif
struct CardContent {std::wstring name,html;double width=480,height=300;};
struct GeneratedCard {
    std::wstring id,name,html,state=L"{}";
    double width=480,height=300,x=48,y=48,w=480,h=300;
    bool visible=false,restored=false,lastRunning=false;
    unsigned long long lastSave=0;
    std::wstring image;
    std::optional<CardContent> previous;
    int layer=0,renderId=0;
    unsigned long long edited=0,contentVersion=0;
};
bool ValidGeneratedCard(const GeneratedCard& card);
std::wstring CardDocument(const GeneratedCard& card);
bool ValidCardState(const std::wstring& json);
std::wstring GeneratedCardsJson(const std::vector<GeneratedCard>& cards);
bool ParseGeneratedCards(const std::wstring& json,std::vector<GeneratedCard>& cards);
std::wstring RevisionInput(const GeneratedCard& card);
bool RevisedCard(const GeneratedCard& current,const GeneratedCard& result,GeneratedCard& replacement);
bool RestorePrevious(const GeneratedCard& current,GeneratedCard& replacement);
bool MatchesRevisionTarget(const GeneratedCard& current,const std::wstring& id,unsigned long long version);
}
