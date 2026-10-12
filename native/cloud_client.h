#pragma once
#include "editor.h"

namespace callayers {
struct CloudAccount {
    bool configured=false,signedIn=false,busy=false,administrator=false;
    std::wstring message=L"Online service is not configured for this build.",email,uid;
};
struct CloudAdminResult { std::string json; std::wstring error; };
// Workers return plain data; UI polling owns all XAML controls.
class CloudAdminJob {
    struct State;
    std::shared_ptr<State> state;
public:
    void Start(const std::wstring& method,const std::wstring& path,const std::string& body={});
    std::optional<CloudAdminResult> TakeResult();
    void Cancel(){state.reset();}
    ~CloudAdminJob(){Cancel();}
};
bool CloudEnabled();
bool CloudAccessAllowed();
void CloudStart();
void CloudStop();
CloudAccount CloudGetAccount();
bool CloudTakeSignInReturn();
void CloudSignIn();
void CloudSignOut();
void CloudRefresh();
// Only the fixed, content-free fields accepted by the collector belong in properties.
void CloudTrack(const wchar_t* event,winrt::Windows::Data::Json::JsonObject properties={});
void CloudTransition(const wchar_t* event,const wchar_t* state);
void CloudFailure(const wchar_t* component,const wchar_t* code,HRESULT nativeCode);
void CloudObserve(bool previewVisible,size_t consumers,const std::vector<std::wstring>& shownCards,size_t annotations);
std::string CloudGenerate(const std::string& providerRequest,const std::string& requestId);
std::string CloudRequestId();
}
