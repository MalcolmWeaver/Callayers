#include "auth_callback.h"
#include <map>
#include <vector>

std::string AuthUrlEncode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

static bool Decode(const std::string& value, std::string& out) {
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '%') {
            if (i + 2 >= value.size()) return false;
            int hi = digit(value[i + 1]), lo = digit(value[i + 2]);
            if (hi < 0 || lo < 0) return false;
            c = char(hi * 16 + lo);
            i += 2;
        } else if (c == '+') c = ' ';
        if (c == '\0' || c == '\r' || c == '\n') return false;
        out += c;
    }
    return true;
}

AuthCallback ParseAuthCallback(const std::string& target, const std::string& expectedState) {
    if (target.size() > 8192 || expectedState.empty()) return {};
    auto question = target.find('?');
    if (target.substr(0, question) != "/callback" || question == std::string::npos) return {};
    std::map<std::string, std::vector<std::string>> values;
    size_t start = question + 1;
    while (start < target.size()) {
        auto end = target.find('&', start);
        if (end == std::string::npos) end = target.size();
        auto field = target.substr(start, end - start);
        auto equal = field.find('=');
        std::string key, value;
        if (equal == std::string::npos || !Decode(field.substr(0, equal), key) || !Decode(field.substr(equal + 1), value)) return {};
        values[key].push_back(value);
        start = end + 1;
    }
    if (values["state"] != std::vector<std::string>{expectedState}) return {};
    if (values.count("error")) {
        if (values["error"].size() != 1 || values.count("code")) return {};
        return {CallbackKind::Cancelled, {}};
    }
    auto codes = values["code"];
    if (codes.size() != 1 || codes[0].empty() || codes[0].size() > 4096) return {};
    return {CallbackKind::Code, codes[0]};
}
