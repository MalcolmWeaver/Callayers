#pragma once
#include <string>

namespace callayers {
struct StoredAuthSession {std::wstring serviceUrl,projectId,uid,refreshToken;};
StoredAuthSession LoadAuthSession();
void SaveAuthSession(const StoredAuthSession& session);
bool ClearAuthSession();
}
