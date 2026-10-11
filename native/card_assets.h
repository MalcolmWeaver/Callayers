#pragma once
#include "common.h"
#include <optional>
#include <set>
#include <sstream>
#include <iomanip>

namespace callayers {
inline constexpr size_t MaxCardAssets=8,MaxCardAssetChars=1024*1024,MaxCardMediaChars=4*1024*1024;
struct CardAsset {
    std::wstring id,description,sourceUrl;
    UINT width=0,height=0;
    std::shared_ptr<const std::wstring> dataUri;
    bool operator==(const CardAsset& other)const{
        return id==other.id&&description==other.description&&sourceUrl==other.sourceUrl&&width==other.width&&height==other.height&&
            (dataUri==other.dataUri||(dataUri&&other.dataUri&&*dataUri==*other.dataUri));
    }
};
struct CardUsage {
    std::wstring model;
    uint64_t input=0,output=0,cached=0,cacheWrite=0,reasoning=0;
    std::optional<double> estimatedUsd;
    bool operator==(const CardUsage&)const=default;
};
inline bool AssetIdCharacter(wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'_';}
inline void ValidateCardAssets(const std::vector<CardAsset>& assets){
    size_t total=0;std::set<std::wstring> ids;
    if(assets.size()>MaxCardAssets)throw std::runtime_error("A card can contain up to eight images.");
    for(const auto& a:assets){
        if(a.id.empty()||a.id.size()>64||!std::all_of(a.id.begin(),a.id.end(),AssetIdCharacter)||!ids.insert(a.id).second||a.description.size()>240||a.sourceUrl.size()>8192||!a.dataUri||a.dataUri->size()>MaxCardAssetChars||a.width>4096||a.height>4096)
            throw std::runtime_error("Invalid card image.");
        const auto& data=*a.dataUri;auto comma=data.find(L',');
        if(!(data.starts_with(L"data:image/png;base64,")||data.starts_with(L"data:image/jpeg;base64,")||data.starts_with(L"data:image/gif;base64,")||data.starts_with(L"data:image/webp;base64,")||data.starts_with(L"data:image/svg+xml;base64,"))||comma==std::wstring::npos||comma+1==data.size()||
            data.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=",comma+1)!=std::wstring::npos)
            throw std::runtime_error("Invalid embedded card image encoding.");
        total+=data.size();if(total>MaxCardMediaChars)throw std::runtime_error("The card's images are too large. Use fewer or smaller images.");
    }
}
inline std::set<std::wstring> ReferencedCardAssets(const std::wstring& html){
    std::set<std::wstring> ids;constexpr std::wstring_view prefix=L"callayers-asset:";
    size_t start=0;while((start=html.find(prefix,start))!=std::wstring::npos){
        start+=prefix.size();size_t end=start;while(end<html.size()&&AssetIdCharacter(html[end]))++end;
        if(end==start)throw std::runtime_error("The generated card contains an invalid image reference. Retry.");
        ids.insert(html.substr(start,end-start));start=end;
    }return ids;
}
inline std::vector<CardAsset> SelectCardAssets(const std::wstring& html,const std::vector<CardAsset>& available){
    std::vector<CardAsset> result;
    for(const auto& id:ReferencedCardAssets(html)){
        auto it=std::find_if(available.begin(),available.end(),[&](const auto& a){return a.id==id;});
        if(it==available.end())throw std::runtime_error("The generated card requested an unavailable image. Retry or request a different image.");
        result.push_back(*it);
    }
    ValidateCardAssets(result);return result;
}
inline std::wstring ResolveCardAssets(const std::wstring& html,const std::vector<CardAsset>& assets){
    auto selected=SelectCardAssets(html,assets);std::wstring result;size_t start=0;
    constexpr std::wstring_view prefix=L"callayers-asset:";
    for(size_t pos;(pos=html.find(prefix,start))!=std::wstring::npos;){
        result.append(html,start,pos-start);size_t end=pos+prefix.size();while(end<html.size()&&AssetIdCharacter(html[end]))++end;
        auto id=html.substr(pos+prefix.size(),end-pos-prefix.size());
        const auto& a=*std::find_if(selected.begin(),selected.end(),[&](const auto& item){return item.id==id;});result+=*a.dataUri;
        if(result.size()>16*1024*1024)throw std::runtime_error("The rendered card is too large.");start=end;
    }
    result.append(html,start);return result;
}
inline std::wstring UsageSummary(const std::optional<CardUsage>& usage){
    if(!usage)return L"";
    std::wostringstream text;text<<L"Last generation: "<<usage->input<<L" in / "<<usage->output<<L" out";
    if(usage->estimatedUsd)text<<L" · ~$"<<std::fixed<<std::setprecision(5)<<*usage->estimatedUsd;
    else text<<L" · cost unavailable";
    return text.str();
}
}
