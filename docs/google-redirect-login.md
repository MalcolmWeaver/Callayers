# Google redirect sign-in

New desktop builds already send a loopback return address. The browser now
automatically navigates to Google's account chooser instead of trying to open a
popup. After Google returns, a fresh redirect result approves the pending login
and returns the one-time grant to the desktop. No second sign-in click or pairing
code is needed for these builds. Older clients retain manual code confirmation.

The pending login ID is saved in tab-scoped session storage for up to ten minutes.
A remembered Firebase account alone cannot approve an automatic connection.
Canceled or unsuccessful redirects show a retry button and never redirect in a
loop. Existing desktop proof/state/grant validation remains in place.

## Required cloud setup before deploying

1. Find the backend's configured `PUBLIC_ORIGIN` (the same service origin embedded
   in the desktop build).
2. In Google Cloud / Google Auth Platform, select the web OAuth client used by
   Firebase Google sign-in. Add `PUBLIC_ORIGIN/__/auth/handler` to its authorized
   redirect URIs, replacing `PUBLIC_ORIGIN` with the actual HTTPS origin.
   Keep existing redirect URIs.
3. In Firebase Authentication settings, ensure that origin's hostname is an
   authorized domain. If the Firebase API key has HTTP referrer restrictions,
   ensure this origin is allowed there too.
4. Deploy the backend. No native changes are required for desktop builds already
   using the loopback flow. Preserve your deployed AI pricing values in the
   environment file before running the existing deployment script.

The backend serves Firebase's `/__/auth/*` helpers through a fixed-upstream proxy
and uses its own hostname as `authDomain`. This follows Firebase's same-origin
redirect guidance so third-party storage restrictions do not break the flow:
https://firebase.google.com/docs/auth/web/redirect-best-practices#proxy-requests

## Verification

From the backend directory:

```powershell
npm.cmd run build
node --test scripts/login-protocol.test.mjs scripts/auth-redirect.test.mjs
```

After the OAuth console change and deployment, test from the Windows app in the
default browser: Google account selection, first-time consent, automatic return,
cancel/retry, and a different account. Repeat with third-party cookies blocked.
Check an expired link and two separate pending login tabs. The local tests mock
Firebase and cannot verify OAuth console configuration or a real browser round trip.

## Future download attribution (not implemented here)

Use the same PostHog project for the download site and desktop events. Capture
explicit website visit/download-click events under the browser's anonymous ID.
After verified sign-in, identify that browser with the same opaque Firebase UID
already used as the backend's desktop `distinct_id`. Arrange shared first-party
identity on related domains, or an explicit validated handoff for unrelated
domains; Cloud Run cannot read a download site's cookies by itself.

Enable the intended PostHog person/identity processing when implementing this:
the current desktop collector explicitly sets `$process_person_profile: false`.
Do not send Google tokens, desktop credentials, or OAuth callback query strings
to analytics. Login must still work if analytics is blocked or unavailable.

This can measure visit -> download click -> authenticated desktop use. A download
click is not proof of completed download or installation. Different browsers,
cleared storage, or blocked analytics create attribution gaps. A download-specific
installer handoff would be separate work if exact install attribution is needed.

Reference: https://posthog.com/docs/product-analytics/identity-resolution
