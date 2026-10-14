#pragma once
#include <string>
namespace callayers {
struct InteractiveState {
    int count=0;
    std::wstring title=L"Your ideas";
    double remaining=300;
};
bool ParseInteractiveState(const std::wstring& json,InteractiveState& result);
std::wstring InteractiveStateJson(const InteractiveState& state);
}
