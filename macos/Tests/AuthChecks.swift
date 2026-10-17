import Foundation
@main struct AuthChecks {
    static func main() throws {
        func check(_ b: Bool, _ m: String) throws { if !b { throw NSError(domain: m, code: 1) } }
        try check(AuthCallback.parse(target: "/callback?state=abc&code=xyz", expectedState: "abc") == .code("xyz"), "Valid callback rejected")
        try check(AuthCallback.parse(target: "/callback?state=bad&code=xyz", expectedState: "abc") == .ignore, "Wrong state accepted")
        try check(AuthCallback.parse(target: "/callback?state=abc&state=abc&code=xyz", expectedState: "abc") == .ignore, "Duplicate state accepted")
        try check(AuthCallback.parse(target: "/callback?state=abc&code=a%0Db", expectedState: "abc") == .ignore, "Control character accepted")
        try check(AuthCallback.parse(target: "/callback?state=abc&error=access_denied", expectedState: "abc") == .cancelled, "Cancel rejected")
        try check(!BackendClient.validService(URL(string: "http://example.com")!), "Insecure service accepted")
        try check(!BackendClient.validService(URL(string: "https://user:pass@example.com")!), "Userinfo accepted")
        try check(BackendClient.validService(URL(string: "http://127.0.0.1:9999")!), "Local test service rejected")
        print("PASS callback state, duplicate rejection, cancel, control characters and service URL validation")
    }
}
