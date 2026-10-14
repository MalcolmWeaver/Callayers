#pragma once
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <functional>
#include "auth_store.h"

namespace callayers {
enum class AuthPhase { SignedOut, Checking, SigningIn, SignedIn };
struct AuthSnapshot {
    AuthPhase phase=AuthPhase::SignedOut;
    bool allowed=false,rememberFailed=false;
    unsigned revision=0;
    std::wstring message,email;
    bool retryAvailable=false;
    bool forgetFailed=false;
};
class Auth {
    enum class Action { Restore, SignIn, Renew, Save };
    struct Job {Action action;unsigned generation;};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Job> pending_;
    std::thread worker_;
    std::atomic<unsigned> generation_{0};
    std::atomic<bool> stopping_{false};
    AuthSnapshot snapshot_;
    std::wstring browserUrl_,idToken_;
    StoredAuthSession session_;
    ULONGLONG renewAt_=0,expiresAt_=0;
    bool busy_=false;
    std::atomic<bool> gate_{false};
    std::function<void()> revoke_;
    void enqueue(Action action);
    void work();
public:
    Auth();
    ~Auth();
    void restore();
    void signIn();
    void retry();
    void cancel();
    void signOut();
    void tick();
    AuthSnapshot snapshot();
    std::wstring takeBrowserUrl();
    std::wstring idToken();
    bool outputAllowed() const {return gate_;}
    void onRevoke(std::function<void()> callback);
};
}
