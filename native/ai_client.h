#pragma once
#include "editor.h"
#include "website.h"
#include <optional>
namespace callayers {
struct AiSettings {std::wstring model=L"gpt-6-luna";};
struct GeneratedCard {std::wstring name,html;std::vector<CardAsset> assets;std::optional<CardUsage> usage;};
struct AiResult {std::optional<GeneratedCard> card;std::wstring error,notice;std::optional<CardUsage> usage;};
AiSettings LoadAiSettings();
void SaveAiSettings(const AiSettings& settings,const std::wstring& newKey);
bool HasAiKey();
std::string BuildCardRequest(const AiSettings&,const CardRequestTarget&,const std::wstring& prompt,const std::optional<WebsiteSource>& source={});
GeneratedCard ParseCardResponse(const std::string& response);
CardRequestTarget CompactCardRequest(const CardRequestTarget& target);
std::optional<CardUsage> ParseCardUsage(const std::string& response,const std::wstring& model);
class AiJob {
    struct State;
    std::shared_ptr<State> state;
public:
    ~AiJob();
    void Start(const AiSettings&,const CardRequestTarget&,const std::wstring& prompt);
    void Cancel();
    std::optional<AiResult> TakeResult();
};
}
