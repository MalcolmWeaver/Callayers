#pragma once
#include <QString>
#include <optional>

struct SavedSession { QString serviceUrl, projectId, uid, refreshToken; };

std::optional<SavedSession> LoadSession(QString& error);
bool SaveSession(const SavedSession& session, QString& error);
bool ClearSession(QString& error);
