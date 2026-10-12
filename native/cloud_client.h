#pragma once
#include "editor.h"

namespace callayers {
struct CloudAccount {
    bool configured=false,signedIn=false,busy=false;
    std::wstring message=L"Online service is not configured for this build.",email,uid;
};
bool CloudEnabled();
void CloudStart();
void CloudStop();
CloudAccount CloudGetAccount();
void CloudSignIn();
void CloudSignOut();
void CloudRefresh();
// Only the fixed, content-free fields accepted by the collector belong in properties.
void CloudTrack(const wchar_t* event,winrt::Windows::Data::Json::JsonObject properties={});
void CloudTransition(const wchar_t* event,const wchar_t* state);
void CloudFailure(const wchar_t* component,const wchar_t* code,HRESULT nativeCode);
void CloudObserve(bool previewVisible,size_t consumers,const std::vector<std::wstring>& shownCards);
std::string CloudGenerate(const std::string& providerRequest,const std::string& requestId);
std::string CloudRequestId();
}
