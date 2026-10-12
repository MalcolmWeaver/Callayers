# Callayers accounts, AI service, and beta rollout

## Implementation status

This change adds the cloud source code, desktop integration, and deployment instructions. No cloud project exists yet. No accounts, secrets, services, or PostHog dashboards have been provisioned or deployed by the agent. The installer remains a separate follow-up.

Per the owner's instruction, do not run automated tests, launch the app, invoke the service, or perform live verification as part of implementing this change. Compilation is allowed. The walkthrough below is for the owner to perform manually after setup.

The defaults approved for this beta are:

- Seven days, beginning with the first AI request authorized by the backend.
- $1 lifetime trial allowance per approved account.
- $10 per UTC calendar month for manually paid accounts.
- $25 per UTC calendar month across the entire backend project.
- One in-flight AI request per account, with a five-second minimum between requests.
- No Stripe, subscription charge, or collection of payment information. “Paid” is a manual access entitlement.

Amounts are estimated provider-spend allowances, not charges to customers. Trial dates and usage are never reset by reinstalling, changing devices, or toggling paid access. Paid status does not itself bypass the Approved checkbox. Separate GCP projects have separate ceilings, so development also has its own cost budget.

## Stack and addresses

- C++/WinUI client; WinHTTP for HTTPS; Windows Credential Manager for desktop sessions.
- TypeScript/Fastify API on Cloud Run, using Firebase Admin SDK and Firestore.
- Firebase Authentication (Google provider) for browser sign-in.
- Firebase Hosting for sign-in/admin pages, bundled with the Firebase web SDK.
- Secret Manager for the AI provider key and PostHog project ingestion key.
- PostHog Cloud for explicit product events; Cloud Logging for backend service failures.

The desktop needs **two public addresses**:

1. `ServiceUrl`: the direct HTTPS Cloud Run origin, without a path. Long AI requests go here to avoid Firebase Hosting's proxy timeout.
2. `WebUrl`: the Firebase Hosting origin, usually `https://PROJECT_ID.web.app`. Browser sign-in and admin pages live here. Short browser API calls use Hosting's same-origin `/v1/**` rewrite.

Neither address is a secret. Production builds embed both at build time. Unconfigured developer builds can use `CALLAYERS_API_URL` and `CALLAYERS_WEB_URL` environment variables. Credentials and pending telemetry are separated by service origin. Never point a configured production build at an emulator or unrelated service.

## Step 1: accounts and project setup

1. Use your long-term Google account to create separate GCP projects for development and production and attach billing. Reuse existing billing if appropriate. Enable MFA on the owner account.
2. In the Firebase console, add Firebase to each **existing** GCP project. Register a web app. Enable Authentication → Google, set the support email, and authorize the project's Hosting domains. Configure Google Auth Platform branding/audience; add tester emails while the OAuth app is in testing. Only basic identity scopes are required.
3. Create Firestore's **default database in Native mode**, in `us-central1`, with production/restricted rules. The shipped rules deny all direct client access. Use the backend and IAM for all account access.
4. Create a PostHog organization and separate dev/production projects. Choose US or EU ingestion and use that matching `POSTHOG_HOST` value. Record the project ingestion key; no PostHog personal API token is needed by the service.
5. Create a dedicated Callayers AI provider project/key and enable its billing. GCP credits do not fund direct OpenAI usage.
6. In GCP Billing, create low budget alerts. These are notifications, not hard caps. The application independently checks its AI ceiling before dispatching calls.

No BigQuery, Pub/Sub, paid PostHog plan, Stripe account, or cloud deck storage is required for this implementation.

## Step 2: prepare deployment

From the repository root in PowerShell:

```powershell
gcloud auth login
gcloud auth application-default login
.\backend\setup-cloud.ps1 -ProjectId YOUR_DEV_PROJECT
```

The script enables services, creates a backend service identity, grants it Firestore/Auth-read permissions, creates a container repository, and creates two empty secrets. It never deploys a service or opens the camera.

In Secret Manager, add a version containing the appropriate value to each secret:

- `callayers-openai`: the dedicated provider API key.
- `callayers-posthog`: the matching PostHog project's ingestion key.

Keep secret values out of commands, source files, and chat. The backend identity can read these two secrets; it does not need project Owner/Editor. The human deployer and Cloud Build identity also need their normal build/deploy permissions. If Cloud Build reports denied artifact writes, grant its actual project build identity Artifact Registry Writer for this repository, rather than granting Owner.

Copy `backend/env.example.yaml` to `backend/env.local.yaml`. Fill in the project ID and Hosting origin; keep `AI_ENABLED: "false"` for the first deployment. The local file is ignored by Git. Do not add secrets to it.

When ready for real generation, enter the current standard-tier input, cached-input, and output USD-per-million-token rates for the selected model, plus a dated `PRICE_VERSION`. Verify the rate against your actual provider account and model. Set `AI_ENABLED: "true"` and redeploy. Missing prices or key leave generation unavailable. The desktop's legacy per-card cost estimate is not the authoritative account ledger.

Rate fields must be conservative enough for the chosen text model and its pricing tiers. The backend sends no tools, images, background jobs, or paid search, forces standard service tier, bounds input UTF-8 bytes, and bounds output tokens (including reasoning). If a settled cost ever exceeds its reservation, further generation pauses automatically. Do not resume until the pricing/limits are corrected.

## Step 3: deploy the development environment

```powershell
.\backend\deploy.ps1 -ProjectId YOUR_DEV_PROJECT
```

This builds the container, deploys Cloud Run in `us-central1`, builds the browser pages, and deploys Hosting, Firestore rules, indexes and expiry policies. It does not run tests or make verification requests. Firebase CLI may ask you to sign in on first use. Run `npx firebase-tools login` from `backend` if needed.

Cloud Run is publicly invokable because desktop clients use application authentication, not Google Cloud IAM. Every sensitive endpoint independently authenticates its caller. The service caps instances at two. Do not remove application authentication because the service has a public URL.

In the Cloud Run console, copy the service URL. If you change regions, update the Firebase Hosting rewrite as well. New Firestore indexes can take time to become ready.

## Step 4: create your administrator

1. Open `https://YOUR_DEV_PROJECT.web.app/admin.html` and sign in with your chosen Google account. The page will initially say it is not an administrator and show its account ID. This creates the Firebase identity.
2. From `backend`, grant that identity admin status using your Google application-default credentials:

```powershell
npm run admin -- YOUR_DEV_PROJECT your-google-email@example.com grant
```

3. Reload the admin page. The server checks the `admins/UID` record on every admin request. A browser checkbox or user-supplied email cannot grant admin rights.
4. To revoke later, run the same command with `revoke`. This tool uses your cloud operator permissions; it is never shipped in the Windows app.

## Step 5: build and open the Windows app yourself

Save your work and **Quit Callayers from the tray** first. Closing its window only hides it; the single-instance behavior will otherwise reopen the older process.

```powershell
.\scripts\build.ps1 -SkipTests -AppName CallayersCloud `
  -ServiceUrl 'https://YOUR_CLOUD_RUN_SERVICE.run.app' `
  -WebUrl 'https://YOUR_DEV_PROJECT.web.app'
.\build\Release\CallayersCloud.exe
```

Without real addresses, File → Account reports that the service is unconfigured. The normal development settings and personal API key path remain available only when no cloud endpoint is configured. A cloud-configured build never falls back to a local provider key.

Camera runtime installation and registration still use the existing development instructions in the main README. This change is not a signed installer. Do not ask new customers to perform development registration as the eventual release flow.

## Manual walkthrough: sign-in, approval, and trial

1. Before signing in, open a local deck and show a card. Confirm preview and camera output remain usable. Try generation: it should ask you to sign in.
2. File → Account → Sign in with Google. Confirm the eight-character pairing code matches the desktop. Type it in the browser and approve. Return to the desktop. Expect “Awaiting beta approval.”
3. In the admin page, find your email, open the account, check **Approved for beta**, leave **Manually paid access** unchecked, and save. Refresh the desktop account. The trial has not started yet.
4. Enable configured AI pricing/key on the backend, then generate one small card. The first authorized request starts the seven-day trial. Open the account in admin: see trial dates and a request with tokens, estimated cost, and status.
5. Revise the card. Check the generated card still uses local image references and state. Generation/revision should preserve the existing selection/undo behavior.
6. Cancel a generation. The desktop should leave your card unchanged and explain that submitted requests may still use allowance. After the backend settles, inspect its ledger entry rather than assuming cancellation costs nothing.
7. Check **Manually paid access** in admin and refresh the desktop. Expect paid access with a $10 monthly allowance. Keep **Approved** enabled. No payment is taken.
8. Turn **Disable AI and desktop service access** on for that account. New cloud calls should be denied; camera output and existing cards should continue locally. Turn it off afterward.
9. In the dev project's Firestore console only, set that unpaid test account's `trialEndsAt` to a past Unix-millisecond timestamp. Generate: expect trial-expired guidance. Reinstalling/signing out should not create a new trial. Use paid access or a new test account afterward; don't modify production usage ledgers casually.

## Manual walkthrough: spending and interrupted requests

- In a development deployment, temporarily lower trial/global configured allowances enough that the maximum request reservation cannot fit. Expect a clear allowance error and **no new provider call**. Restore the normal configuration afterward.
- Trigger two generations from two app installations signed into the same account. The second should be rejected while the first runs. An account cannot race past its budget because reservations are transactional.
- Interrupt network access after submitting a request, then restore it. The desktop polls the original request ID; the backend must show only one provider request for that ID. If the outcome stays unknown, stop and inspect the admin ledger before generating again.
- Use **Pause all AI** in admin. Expect new generations to stop while camera output continues. Resume afterward. This stops new calls, not already-dispatched ones.
- “Pending” reservations after a backend interruption remain counted. Provider timeouts become conservative “uncertain” charges. There is intentionally no automatic timed refund that could enable overspending. Reconciliation is an operator follow-up; the first release has no refund/reconcile button.

## Manual walkthrough: lifecycle and card events

In PostHog's event explorer, filter by the Firebase user ID shown on the admin/account page. Allow 30–60 seconds for batching, plus service ingestion delay.

1. Launch: `process_state_changed / started`, initial preview and vcam states.
2. Minimize: `preview_state_changed / hidden`; restore: `visible`. Close to tray: hidden, with no process-end event.
3. Select Callayers in another app: `vcam_state_changed / consuming`. Stop the last consumer: `idle`. Multiple consumers should not produce a false idle state when only one stops.
4. First observed consumer on this installation/service: `setup_completed`. This proves a consumer connected, not that someone joined a meeting.
5. Check a card while no consumer exists: `card_stage_changed / shown`, with no output event. Start a consumer: `card_output_changed / shown`. Uncheck it: corresponding removed events.
6. Generate/revise: `card_generation` includes operation, request ID, and started/succeeded/failed/canceled/discarded state. Authoritative billing remains in the backend ledger.
7. Quit from the tray: best-effort `process_state_changed / ended`. Force termination may lose the unflushed tail. A missing end is not proof of a crash. Minute heartbeats provide approximate process lifetime, not active presentation time.

`card_output_changed` means a selected card is included in the intended output scene while a consumer lease exists; it does not prove the receiving app displayed the frame. Stage visibility is independent of whether the editor window is minimized. Unchanged states do not generate per-frame events.

The first release sends analytics only with an authenticated desktop session. It can queue pre-sign-in launch events and associate them with the first sign-in. People who never sign in are not visible in PostHog. Queues are bounded to 1,000 events, encrypted for the Windows user, separated by service origin, and persisted asynchronously. Events older than seven days are discarded at intake. Switching accounts clears events belonging to another account. Sign-out clears pending events. Delivery retries retain UUIDs for deduplication; loss remains possible after abrupt termination, quota overflow, or disk failure.

Create three initial PostHog insights manually: first `setup_completed` users; weekly returning `card_output_changed` users filtered to `state=shown`; failures grouped by `component`, `code`, and `app_version`. For retention, exclude staff/test accounts. Disable replay/autocapture; this app supplies explicit events.

## Manual walkthrough: recovery messages

- Deny desktop camera access in Windows Settings, then select the camera again. Expect permission guidance on screen and one `failure` event with a stable code and HRESULT. Repeated timer ticks should not flood the collector.
- Restore permission and select the webcam again. Recovery should be emitted once fresh frames return.
- Disconnect the selected webcam. Expect an explicit failure or waiting/idle state rather than a claim of successful live video. Select another camera or Test pattern; do not assume the app automatically switches devices.
- Stop a camera-consuming app and reopen it. Observe consumer stop/start independently of preview visibility.
- For a card-renderer failure, expect save/restart guidance and a renderer failure record. Full crash dumps, symbolication, GPU performance tracing, and recovery slates matching Puppetbooth's complete rendering behavior are not implemented in this slice.

Failure events contain component, stable code, native error code, app version and session identifiers. They do not upload screen-message text, paths, URLs, prompts, or card HTML. Admin AI request records separately contain provider error categories and request IDs. Cloud logs deliberately omit request bodies and authorization headers.

## Sessions, retention, and limits

Browser identity is verified with Firebase Admin, including revocation checking and Google provider/email verification. Pairing expires after ten minutes and requires a secret known only to the initiating desktop. The browser sees a request identifier and confirmation code, not a reusable desktop credential. Approved desktop sessions expire after 30 days; this first version asks for sign-in again instead of implementing silent long-lived refresh. Backend stores only session hashes. Account disable is checked on every desktop call.

Request ledgers contain metadata and usage, not prompts. For connection recovery, generated API responses (including the generated card HTML) are stored for 24 hours in a separate collection; reads reject expired results even before TTL deletion. Firestore TTL deletion is asynchronous. Install/usage events go to PostHog under opaque account IDs without email/profile autocapture. Choose PostHog retention/access appropriate to your beta. Account metadata, billing ledger and admin audit records remain until an operator deletes them; a self-service account-deletion workflow is a follow-up.

This spending protection bounds **AI requests at the configured prices**, not the entire cloud bill. Network attacks, logs, Firestore reads/writes, and downloads still require cloud billing monitoring. Invitation approval prevents account creation alone from granting AI spend. Rate limiting and small instance limits are basic beta controls, not a complete public-launch anti-abuse system.

## Production and installer follow-up

Repeat setup/deployment for the production project with its own secrets, Firebase users, PostHog project, admin grant and budget. Embed production addresses in the release build. Perform the walkthrough manually in development before inviting production testers.

The installer follow-up covers signing, runtime prerequisites, machine-wide camera registration, safe update while the DLL is loaded, clean uninstall, release hosting/version metadata, and a clean-Windows install check. No installer or automatic updater was added here.
