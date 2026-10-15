#pragma once
#include <string>

enum class CallbackKind { Ignore, Code, Cancelled };
struct AuthCallback { CallbackKind kind = CallbackKind::Ignore; std::string code; };

AuthCallback ParseAuthCallback(const std::string& target, const std::string& expectedState);
std::string AuthUrlEncode(const std::string& value);
