# Callayers v2 API

Fresh code for the sign-in milestone in `spec-v2`. Cloud Run service `callayers-v2-api` uses only the named Firestore database `callayers-v2` in the existing Google project `callayers`. The original service and default database are not migration inputs. Firebase Authentication identities and the verified installed-app Google OAuth client are shared.

Implemented: `/health`, public `/client-config`, and `/account`. Every account request verifies a Firebase ID token, including revocation/disabled-user checks, then validates Google provider, project and account access. Accounts are created transactionally once. Only access, signup date and daily usage are stored. No credentials, prompts, card HTML, annotations or webcam images enter Firestore or operational logs. Generation, request records, telemetry, trial gates and payments remain later milestones. The allowance response explicitly reports generation unavailable until that milestone sets a daily limit.

The native desktop uses PKCE and a temporary loopback callback. `/client-config` includes the installed-app client ID/secret and Firebase API key: Google treats installed-app client secrets as public configuration, since an installed client cannot keep a shared secret confidential. This is not a server credential. Cloud Run uses its service identity for Firebase/Firestore; server provider secrets are never returned by this endpoint. The one-time setup script reads only the supplied downloaded desktop-client JSON and stores it in a dedicated Secret Manager entry. No service-account keys are downloaded or shipped.

The runtime service account has Firebase Auth read access, Firestore user access conditional on the v2 database, and access to its own OAuth configuration secret. A separate v2 build identity can read only the v2 uploaded-source prefix, write the `cloud-run-source-deploy` artifact repository, and write operational logs. The old service's `callayers` artifact repository is separate. No project-wide Cloud Run Builder role is granted. Firestore client rules deny all direct access; server requests use IAM. Source deployment runs with Node 22, zero minimum instances and two maximum instances. Standard Google Cloud usage charges apply.

From the repository root, build the workspace, then perform the one-time setup:

```powershell
.\scripts\build.ps1 -WorkspaceBuild
.\scripts\setup-backend-v2.ps1 -OAuthJsonPath 'C:\path\to\downloaded-desktop-client.json'
.\scripts\deploy-backend-v2.ps1
```

Setup and deployment are separate commands. `setup-backend-v2.ps1` configures the database, access rules, service identities, permissions and OAuth entry; it does not install dependencies, run tests or deploy. To finish only the missing build permissions after the database/runtime/OAuth setup is complete, run `.\scripts\setup-backend-v2.ps1 -BuildOnly`; it does not reread or reupload the OAuth file.

Routine deployment is `.\scripts\deploy-backend-v2.ps1`. It installs pinned dependencies, runs backend tests, deploys this service only, checks public endpoints and unauthorized access, and writes `service-url.txt` beside the workspace executable. It does not create the database or service identities, change setup permissions, or read/upload the OAuth file. Cloud Run applies the service's public invocation setting during deployment. `SERVICE_URL` overrides the address file for explicit development testing. No fallback to the old service exists.

Backend tests: `npm test` inside `backend-v2`. They use local synthetic identities/account storage, exercise actual HTTP responses, and do not authenticate or create cloud records. Native authentication tests check the RFC 7636 PKCE vector, malformed/duplicate callbacks, random state generation and a synthetic current-user DPAPI roundtrip. User verification of Google sign-in, camera gating, return-to-app, restart and sign-out is provided in chat when deployment is ready.

References: [Google installed-app OAuth](https://developers.google.com/identity/protocols/oauth2/native-app), [installed-app secret confidentiality](https://developers.google.com/identity/protocols/oauth2), [Firebase token verification](https://firebase.google.com/docs/auth/admin/verify-id-tokens), [named databases and IAM conditions](https://firebase.google.com/docs/firestore/manage-databases), [Cloud Run source deployment](https://docs.cloud.google.com/run/docs/deploying-source-code).
