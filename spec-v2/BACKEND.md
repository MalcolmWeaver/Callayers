# Backend

One backend supports sign-in, AI generation, allowances and MVP telemetry. Editing cards and using the camera happen locally.

## Backend endpoints

| Endpoint | Purpose |
| --- | --- |
| `GET /client-config` | Settings needed for Google sign-in |
| `GET /account` | Account access and remaining AI allowance |
| `POST /generate` | Create or revise a card |
| `POST /events` | Receive and forward approved authenticated telemetry |
| `GET /health` | Check deployment health and version |

The native sign-in button opens Google directly in the default browser. A temporary local callback returns the result to Callayers and serves the completion message. No hosted login page. Google/Firebase authentication and renewal have their own supporting network calls.

Validate the remembered login before unlocking at launch. A temporary backend outage must not interrupt an already running call. Explicit sign-out or confirmed invalid credentials stop output.

Camera selection, image import, show/hide, arrangement, annotations, rename, and delete do not depend on backend requests.

There is no result-recovery endpoint. Repeating a generation request ID returns its status only; a lost response requires an explicit new generation. See [DATA.md](DATA.md).

## Trial and paid access (later)

No trial timer, payment flow, or new access gate now. Keep the existing sign-in/account check and one shared way to stop virtual-camera output.

When trials launch, an expired trial without paid access must stop virtual-camera output, including during an open session. The minimum addition is a backend-provided access deadline, checks at launch/output start and after wake, and a local expiry timer. Payment confirmation refreshes access. Clear the last live frame and show a neutral unavailable image; explain expiry and Subscribe inside the app. Preserve local cards.

Trial length, renewal/offline behavior, and AI/editor access after expiry must be decided before launching trials. Signed offline grants and additional anti-tampering measures are not part of the initial scope.

## Telemetry (MVP)

The four approved event families and timing semantics live in [DATA.md](DATA.md#telemetry-mvp). `POST /events` uses the existing Google/Firebase verification and enabled-account check, derives the owner on the server, and accepts only the exact envelope/properties. Reject unknown fields/events and private data, batches outside 1..32 events, bodies over 32 KiB, invalid IDs and timestamps older than 24 hours or more than ten minutes ahead. Telemetry does not reserve AI allowance or create Firestore telemetry records.

Forward validated batches to the selected PostHog cloud region with a four-second timeout and no redirects. Return success only after ingestion accepts; failures return generic retryable responses without tokens/upstream exceptions/content in logs. Stable UUID/event/timestamp/owner support eventual deduplication. Use bounded per-instance protection (600 events per account per minute; at most 1024 active windows). This is best-effort analytics, not an exactly-once audit trail. No extra service, persistent queue, worker or scheduler.

`POSTHOG_HOST` and Secret Manager-bound `POSTHOG_PROJECT_TOKEN` are configured separately from routine deployment. `/health` reports `telemetryReady`; unconfigured telemetry returns 503 without preventing sign-in/generation. There is no user telemetry-off setting. No keystroke, annotation-edit, replay or automatic browser events.

## Services and deployment

Use one Google project: Cloud Run hosts the API, Firebase handles sign-in, Firestore holds service records, and Secret Manager holds credentials. Cloud Build/Artifact Registry support deployment; Cloud Logging captures operational errors. OpenAI generates cards; PostHog holds product events.

**One-time setup:** configure a Google Desktop OAuth client with Callayers branding and Firebase acceptance of its credentials; configure database access/expiry, service permissions, and secrets; deploy; verify sign-in, return to app, generation, and allowances. Add PostHog and verify event arrival when implementing telemetry.

**Routine deployment:** run tests, deploy one service, verify health/version and sign-in. Use one script and standard source deployment. No extra frontend host, worker, queue, scheduler, or admin app. Compatible backend changes do not require rebuilding the desktop.

Manage runtime environment variables and secret bindings directly in Cloud Run, independently of routine deployment. Model, optional reasoning effort and daily allowance belong to backend startup configuration. Routine deployments must preserve these choices, service permissions, public invocation and capacity settings. Provider secret access and request expiry are explicit setup operations. Keep generation instructions in a reviewed repository prompt file; changing that file deploys code and affects newly generated cards.

### Sign in implementation

Use authorization-code flow with PKCE, random state, and a temporary loopback listener. Accept the matching callback once; exchange the Google credential for a Firebase session and validate account access. Then show success and attempt to foreground Callayers. If Windows leaves the browser in front, the completion message tells the user to return manually. Cancellation/timeout keeps the app locked. Store the session securely; never log codes or tokens. Test the desktop-client/Firebase exchange before building the full flow.

### Account and request handling

Use standard verified Firebase credentials and enforce request ownership. Create accounts only once. Reserve each user's daily AI allowance atomically and prevent duplicate generation for repeated request IDs. Network operations stay off UI/video threads. Refresh accounts when relevant; do not poll every minute.

References: [Cloud Run deployment](https://docs.cloud.google.com/run/docs/deploying-source-code), [Google desktop OAuth](https://developers.google.com/identity/protocols/oauth2/native-app), [Firebase credential exchange](https://firebase.google.com/docs/reference/rest/auth#section-sign-in-with-oauth-credential), [PostHog API](https://posthog.com/docs/api).
