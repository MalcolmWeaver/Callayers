#include "auth_protocol.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace callayers;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try {
    require(AuthPkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")=="E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM","RFC7636 PKCE vector");
    auto state=AuthRandomToken(32),second=AuthRandomToken(32),verifier=AuthRandomToken(48);
    require(state!=second && state.size()==43 && verifier.size()==64,"Independent state/verifier lengths");
    require(std::all_of(verifier.begin(),verifier.end(),[](char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';}),"PKCE alphabet");
    require(AuthUrlEncode("a b+&=/~")=="a%20b%2B%26%3D%2F~","Form escaping");
    auto callback=ParseAuthCallback("/callback?code=a%2Bb%2Fc&state=expected","expected");
    require(callback.kind==CallbackKind::Code && callback.code=="a+b/c","Callback decoding");
    require(ParseAuthCallback("/callback?error=access_denied&state=expected","expected").kind==CallbackKind::Cancelled,"Matched cancellation");
    for(const char* target:{"/other?code=c&state=expected","/callback?code=c&state=wrong","/callback?code=c&state=expected&state=expected",
        "/callback?code=c&code=d&state=expected","/callback?code=c&error=denied&state=expected","/callback?code=%0A&state=expected",
        "/callback?code=%00&state=expected","/callback?code=%ZZ&state=expected","/callback?code=&state=expected","/callback?code=c"})
        require(ParseAuthCallback(target,"expected").kind==CallbackKind::Ignore,"Ignore malformed/unmatched callback");
    const std::string synthetic="{\"refreshToken\":\"synthetic-test-data\"}";
    auto encrypted=ProtectAuthSession(synthetic);require(UnprotectAuthSession(encrypted)==synthetic,"Current-user DPAPI roundtrip");
    require(std::search(encrypted.begin(),encrypted.end(),synthetic.begin(),synthetic.end())==encrypted.end(),"Encrypted data has no plaintext");
    encrypted.back()^=0x55;bool rejected=false;try{UnprotectAuthSession(encrypted);}catch(...){rejected=true;}require(rejected,"Tampered credentials rejected");
    std::cout<<"PKCE, callback validation and current-user credential protection passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
