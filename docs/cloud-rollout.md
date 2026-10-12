# Callayers cloud setup and manual walkthrough

## Status and architecture

The code now uses one Cloud Run origin for the API and the small browser Google sign-in page. Administration lives inside the Windows app, under File → Account → Administration. Every admin request uses the desktop session and checks the server's `admins/UID` record. Hiding the button is not the authorization boundary.

The simplification has not been built, tested, launched, deployed, or otherwise verified by the agent. All steps below are for you to run manually. Existing executable files do not contain these changes.

Use the single project **callayers** for now. Its shared accounts and spending ceilings apply to both local development builds and beta builds. Keep the existing `(default)` Firestore Native database in `nam5`; do not recreate it. Based on your reported setup, the cloud resources and both Secret Manager values are already present. This walkthrough does not assume a deployed backend or a registered Firebase web app.

Stack: Windows C++/WinUI; Cloud Run with TypeScript/Fastify; Firebase Authentication with Google; Firestore for accounts, sessions and AI usage; Secret Manager for provider and PostHog keys; PostHog for product events; Cloud Logging for service errors. No BigQuery, Stripe, or separate Firebase Hosting deployment is needed. Installer/signing/updates remain a follow-up.

Firebase Authentication still uses its managed `callayers.firebaseapp.com` domain as the Google sign-in helper. That is independent of publishing our own site to Firebase Hosting. An existing empty Hosting site can stay; this change does not delete cloud resources.

## 1. Finish public Firebase configuration

In Firebase Console, open **callayers → Project settings → General → Your apps**. If there is no web app, register one (for example “Callayers sign-in”). Do not select Firebase Hosting. Copy the `apiKey` from its Firebase configuration. This is public Firebase client configuration, not an OpenAI or PostHog secret.

Keep Authentication → Google enabled. The native app uses this browser sign-in; you do not create a separate local account or install Firebase on your PC.

From `C:\Users\malco\callayers` in PowerShell, create the local config only if it does not exist:

```powershell
if (-not (Test-Path .\backend\env.local.yaml)) {
    Copy-Item .\backend\env.example.yaml .\backend\env.local.yaml
}
notepad .\backend\env.local.yaml
```

Set these entries (keep the other defaults):

```yaml
GOOGLE_CLOUD_PROJECT: "callayers"
FIREBASE_WEB_API_KEY: "PASTE_FIREBASE_WEB_API_KEY"
AI_ENABLED: "false"
POSTHOG_HOST: "https://us.i.posthog.com"
```

No provider secrets belong in this file. `deploy.ps1` sets `PUBLIC_ORIGIN` to the actual Cloud Run URL, replacing any old Hosting origin. The service's `callayers-posthog` secret should contain the project ingestion token (`phc_`), not a privileged secret/personal API key. Your screenshot's “Create project secret API key” dialog is not needed for event capture.

## 2. Deploy the backend yourself

The Firebase CLI needs a signed-in operator. If not already signed in:

```powershell
npx.cmd firebase-tools login
```

Then from the repository root:

```powershell
.\backend\deploy.ps1 -ProjectId callayers
```

This builds the container including the sign-in page, deploys Cloud Run in `us-central1`, sets its public origin, and deploys Firestore rules/indexes/expiry policies. It does not launch the desktop app or make verification requests. Wait for required Firestore indexes to finish building before opening account request details.

Copy the **Service URL** printed at the end. In Firebase → Authentication → Settings → Authorized domains, add its hostname (no `https://`, no path). Keep `callayers.firebaseapp.com` authorized. If the Google OAuth consent configuration is still in testing, add the Google accounts you intend to use as testers.

Cloud Run allows public invocation because our API validates app sessions itself. The runtime identity remains `callayers-api@callayers.iam.gserviceaccount.com`. If deployment reports a permission error, use the error to identify the actual deployer/build identity and missing permission; don't rerun setup against another project. All project commands here specify `callayers` explicitly.

## 3. Build and launch locally yourself

Save your deck and **Quit Callayers from the tray**. Closing its window only hides the running process.

Replace the example URL below with the exact Service URL from deployment:

```powershell
.\scripts\build.ps1 -SkipTests -AppName CallayersCloud `
  -ServiceUrl 'https://YOUR_ACTUAL_CLOUD_RUN_URL.run.app'
.\build\Release\CallayersCloud.exe
```

`-SkipTests` builds without executing the automated tests. There is no `-WebUrl` argument anymore. This is a local Windows app connected to the deployed backend; no local backend or emulator is required. Existing camera runtime/registration prerequisites are in the main README.

A build without `-ServiceUrl` retains the original developer API-key flow. An unconfigured build can also take `CALLAYERS_API_URL` from its launch environment. A cloud-configured build never falls back to a personal provider key.

## 4. Verify sign-in, then grant your admin role

1. On a fresh cloud-configured launch, expect a sign-in dialog. The editor and camera output should remain unavailable until the saved session is validated or Google sign-in completes. Escape should not dismiss this gate.
2. Choose **Sign in with Google** in the startup dialog (or File → Account when already signed in). The browser should open the Cloud Run hostname, then Google sign-in.
3. Sign in as `malcolm@callayers.com`. Match and type the eight-character code displayed in the desktop app, then choose **Connect Callayers**. Return to the app; expect **Awaiting beta approval**. This creates your Firebase identity and backend account.
4. Grant your first admin role using your cloud operator credentials. Run these commands yourself:

```powershell
gcloud auth application-default login
Push-Location .\backend
npm.cmd ci --no-audit --no-fund
npm.cmd run admin -- callayers malcolm@callayers.com grant
Pop-Location
```

The ADC login is for this local operator command, not a service-account key distributed with the app.

5. In the app, select **Refresh account**. **Administration** should appear. Open it; expect monthly global spending and the top lifetime spenders.
6. Search your exact Google email. Open your account, check **Approved for AI**, leave **Paid access** off, and choose **Save access**. Confirm the refreshed details retain the change. The trial should still say **not started** while AI is disabled.
7. Close Administration and reopen Account. Expect approved trial access, the $1 allowance, and “AI service not enabled yet.” Restart the app and confirm it validates your saved session and then opens the editor. Sign out: expect the gate and stopped camera output, with the unsaved deck kept in memory. Sign back in to resume.

To revoke admin later, use the same operator command with `revoke`. Refresh Account: the button should disappear. With Administration already open, a subsequent admin request should be denied and the panel should clear on its next UI update. Regrant with `grant` if this is your admin account.

Use a second Google account to confirm ordinary users do not see Administration. That account should need your approval before AI use. The backend checks admin status on every admin request, even if an older client still displays the button.

## 5. Verify paid access and AI manually

First confirm basic sign-in/admin works with `AI_ENABLED: "false"`; this phase should make no provider calls.

To enable real generation, enter the selected model's **current, verified standard-tier** input, cached-input and output USD-per-million-token rates in `env.local.yaml`, plus a dated `PRICE_VERSION`; then set `AI_ENABLED: "true"` and redeploy. Do not guess these rates: they determine reservations and allowance enforcement. Missing rates deliberately block generation. OpenAI billing is separate from the GCP credits.

Defaults:

- Seven days starting with the first backend-authorized AI request.
- $1 lifetime trial allowance per approved user.
- $10 per UTC calendar month for manually paid accounts.
- $25 per UTC calendar month across this one shared project.
- One in-flight AI request per account, with a five-second minimum between requests.

Amounts are estimated provider spend, not charges to users. Paid status still requires Approved for AI. Reinstalling or toggling paid access does not reset trial dates or usage.

Manual checks:

1. Generate a small card, then revise it. Confirm the local card and undo behavior. In Administration, refresh that account's details: expect trial end date, request status, input/output tokens and estimated cost.
2. Check Paid access and Save access. Refresh Account: expect paid status and remaining monthly allowance. No payment is collected. Turn it off if you want to keep this account on trial.
3. Disable a **second test account**, then try cloud operations from that account. Expect rejection and the app should return to the sign-in gate after its next account refresh (about a minute). Re-enable it afterward. The UI/backend prevent disabling your own administrator account.
4. Pause AI in Administration and attempt a generation. Expect denial of a new request while camera output continues. Resume afterward. Resuming does not override `AI_ENABLED`, missing prices, or budgets. Already-dispatched calls may finish.
5. Cancel a generation: the card should stay unchanged; the UI should explain submitted work may still use allowance. Inspect the ledger after settlement.
6. Interrupt connectivity during generation, then restore it. Recovery should use the original request ID, not automatically buy another generation. If the outcome remains unknown, inspect that ledger entry before starting another request.
7. Before inviting other users, you can temporarily lower an allowance below a maximum request reservation and redeploy to exercise budget rejection. Expect an allowance error and no new provider call. Restore defaults afterward. Because dev/prod share one project, this affects everyone; do it before invitations or during an agreed pause.

Pending reservations remain counted after backend interruptions. Unknown provider outcomes use conservative charges. There is no automatic refund or reconciliation button. If settled cost exceeds a reservation, the backend pauses generation; correct rates/limits before resuming.

## Manual walkthrough: lifecycle and card events

In PostHog's event explorer, filter by the Firebase user ID shown on the native Administration account details. Allow 30–60 seconds for batching, plus service ingestion delay.

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

Browser identity is verified with Firebase Admin, including revocation checking and Google provider/email verification. Pairing expires after ten minutes and requires a secret known only to the initiating desktop. The browser sees a request identifier and confirmation code, not a reusable desktop credential. Approved desktop sessions expire after 30 days; this first version asks for sign-in again instead of implementing silent long-lived refresh. Backend stores only session hashes. Account disable is checked on every desktop call. The app refreshes its account about once a minute; a rejected session locks the editor and camera. A fresh process needs an online account check before unlocking. After a successful check, a transient network failure does not itself sign you out.

Request ledgers contain metadata and usage, not prompts. For connection recovery, generated API responses (including the generated card HTML) are stored for 24 hours in a separate collection; reads reject expired results even before TTL deletion. Firestore TTL deletion is asynchronous. Install/usage events go to PostHog under opaque account IDs without email/profile autocapture. Choose PostHog retention/access appropriate to your beta. Account metadata, billing ledger and admin audit records remain until an operator deletes them; a self-service account-deletion workflow is a follow-up.

This spending protection bounds **AI requests at the configured prices**, not the entire cloud bill. Network attacks, logs, Firestore reads/writes, and downloads still require cloud billing monitoring. Invitation approval prevents account creation alone from granting AI spend. Rate limiting and small instance limits are basic beta controls, not a complete public-launch anti-abuse system.

## Installer and release follow-up

Do the manual checks above before giving cloud-enabled builds to beta users. Exclude your own/test accounts from PostHog retention reports because development and production currently share the project. Keep the same service URL embedded in release builds.

The installer follow-up covers signing, prerequisites, machine-wide camera registration, updating while the DLL is loaded, uninstall, download/version metadata, and a clean-Windows install check. This change adds neither an installer nor an automatic updater.

## Invalid Firebase API key

If the browser shows `auth/api-key-not-valid`, open Firebase Project settings → General → Your apps → the web app's SDK configuration. Use its `apiKey` (starts with `AIza`), not the app ID, project ID, OpenAI key or PostHog key. Replace `FIREBASE_WEB_API_KEY` in `backend/env.local.yaml` too, so future deployments retain it.

To repair the already-deployed service without another image build, run:

```powershell
$firebaseWebApiKey = Read-Host 'Paste the Firebase web app apiKey'
gcloud run services update callayers-api --project callayers --region us-central1 --update-env-vars "FIREBASE_WEB_API_KEY=$firebaseWebApiKey"
Remove-Variable firebaseWebApiKey
```

Start sign-in again from the desktop app after the update. An old pairing link may have expired. Rebuild the desktop to pick up the sign-in gate; updating server configuration alone does not change an existing executable.
