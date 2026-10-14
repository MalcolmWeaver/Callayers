#pragma once
#include <string>
#include <vector>
namespace callayers {
struct GeneratedCard {
    std::wstring id,name,html,state=L"{}";
    double width=480,height=300,x=48,y=48,w=480,h=300;
    bool visible=false,restored=false,lastRunning=false;
    unsigned long long lastSave=0;
};
bool ValidGeneratedCard(const GeneratedCard& card);
bool ValidCardState(const std::wstring& json);
std::wstring GeneratedCardsJson(const std::vector<GeneratedCard>& cards);
bool ParseGeneratedCards(const std::wstring& json,std::vector<GeneratedCard>& cards);
}
