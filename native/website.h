#pragma once
#include <string>
#include <atomic>
namespace callayers {
struct WebsiteSource {std::wstring url,text;bool truncated=false;};
std::wstring WebsiteUrl(const std::wstring& prompt);
std::wstring WebsiteText(const std::wstring& html);
WebsiteSource FetchWebsite(const std::wstring& url,const std::atomic<bool>& canceled);
}
