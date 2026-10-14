#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace callayers {
std::string AuthUrlEncode(const std::string& value);
std::string AuthRandomToken(size_t bytes);
std::string AuthPkceChallenge(const std::string& verifier);
enum class CallbackKind { Ignore, Code, Cancelled };
struct AuthCallback {CallbackKind kind=CallbackKind::Ignore;std::string code;};
AuthCallback ParseAuthCallback(const std::string& target,const std::string& expectedState);
std::vector<uint8_t> ProtectAuthSession(const std::string& json);
std::string UnprotectAuthSession(const std::vector<uint8_t>& bytes);
}
