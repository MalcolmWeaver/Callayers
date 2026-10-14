#include "auth_protocol.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <map>
#include <stdexcept>
#include <algorithm>

namespace callayers {
static std::string Base64Url(const std::vector<uint8_t>& bytes) {
    static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;unsigned value=0,bits=0;
    for(auto byte:bytes){value=(value<<8)|byte;bits+=8;while(bits>=6){bits-=6;out+=alphabet[(value>>bits)&63];}}
    if(bits)out+=alphabet[(value<<(6-bits))&63];return out;
}
std::string AuthRandomToken(size_t bytes) {
    if(bytes<16 || bytes>128)throw std::runtime_error("Invalid random length");
    std::vector<uint8_t> buffer(bytes);
    if(BCryptGenRandom(nullptr,buffer.data(),ULONG(buffer.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Random source unavailable");
    return Base64Url(buffer);
}
std::string AuthPkceChallenge(const std::string& verifier) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable");
    std::vector<uint8_t> digest(32);
    NTSTATUS result=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(verifier.data())),ULONG(verifier.size()),digest.data(),ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);if(result<0)throw std::runtime_error("SHA256 failed");return Base64Url(digest);
}
std::string AuthUrlEncode(const std::string& value) {
    static constexpr char hex[]="0123456789ABCDEF";std::string out;
    for(unsigned char c:value){if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='-' || c=='_' || c=='.' || c=='~')out+=char(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}
    return out;
}
static bool Decode(const std::string& value,std::string& out) {
    auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    for(size_t i=0;i<value.size();++i){char c=value[i];if(c=='%'){if(i+2>=value.size())return false;int hi=digit(value[i+1]),lo=digit(value[i+2]);if(hi<0 || lo<0)return false;c=char(hi*16+lo);i+=2;}else if(c=='+')c=' ';if(c=='\0' || c=='\r' || c=='\n')return false;out+=c;}return true;
}
AuthCallback ParseAuthCallback(const std::string& target,const std::string& expectedState) {
    if(target.size()>8192 || expectedState.empty())return {};
    auto question=target.find('?');if(target.substr(0,question)!="/callback" || question==std::string::npos)return {};
    std::map<std::string,std::vector<std::string>> values;
    size_t start=question+1;
    while(start<target.size()){
        auto end=target.find('&',start);if(end==std::string::npos)end=target.size();auto field=target.substr(start,end-start);auto equal=field.find('=');
        std::string key,value;if(equal==std::string::npos || !Decode(field.substr(0,equal),key) || !Decode(field.substr(equal+1),value))return {};
        values[key].push_back(value);start=end+1;
    }
    if(values["state"]!=std::vector<std::string>{expectedState})return {};
    if(values.count("error")){if(values["error"].size()!=1 || values.count("code"))return {};return {CallbackKind::Cancelled,{}};}
    auto codes=values["code"];if(codes.size()!=1 || codes[0].empty() || codes[0].size()>4096)return {};
    return {CallbackKind::Code,codes[0]};
}
std::vector<uint8_t> ProtectAuthSession(const std::string& json) {
    if(json.empty() || json.size()>65536)throw std::runtime_error("Invalid session size");
    DATA_BLOB plain{DWORD(json.size()),reinterpret_cast<BYTE*>(const_cast<char*>(json.data()))},encrypted{};
    if(!CryptProtectData(&plain,L"Callayers session",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted))throw std::runtime_error("Session protection failed");
    std::vector<uint8_t> bytes(encrypted.pbData,encrypted.pbData+encrypted.cbData);LocalFree(encrypted.pbData);return bytes;
}
std::string UnprotectAuthSession(const std::vector<uint8_t>& bytes) {
    if(bytes.empty() || bytes.size()>131072)throw std::runtime_error("Invalid protected session");
    DATA_BLOB encrypted{DWORD(bytes.size()),const_cast<BYTE*>(bytes.data())},plain{};
    if(!CryptUnprotectData(&encrypted,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain))throw std::runtime_error("Session could not be read");
    std::string json(reinterpret_cast<char*>(plain.pbData),plain.cbData);SecureZeroMemory(plain.pbData,plain.cbData);LocalFree(plain.pbData);return json;
}
}
