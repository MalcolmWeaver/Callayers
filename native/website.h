#pragma once
#include <string>
#include <atomic>
#include "card_assets.h"
namespace callayers {
struct WebsiteSource {
    std::wstring url,text;bool truncated=false;
    std::wstring styleSummary,notice;
    std::vector<CardAsset> assets;
};
struct WebsiteImageCandidate {std::wstring url,description;bool logo=false;};
struct WebsiteMetadata {std::wstring css;std::vector<std::wstring> stylesheets;std::vector<WebsiteImageCandidate> images;};
WebsiteMetadata InspectWebsite(const std::wstring& html,const std::wstring& url);
std::wstring SummarizeWebsiteStyles(const std::wstring& css);
std::wstring ResolveWebsiteUrl(const std::wstring& base,const std::wstring& reference);
CardAsset DecodeWebsiteImage(const std::string& bytes,const std::wstring& contentType);
std::wstring WebsiteUrl(const std::wstring& prompt);
std::wstring WebsiteText(const std::wstring& html);
WebsiteSource FetchWebsite(const std::wstring& url,const std::atomic<bool>& canceled);
}
