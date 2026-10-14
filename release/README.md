# Tester release operations

Step 10 implementation uses an unsigned per-user Inno installer, versioned app directories, Windows PowerShell 5.1 for a short-lived updater, and RSA/SHA-256 authenticated release manifests. Windows publisher signing is separate. The updater checks at app startup, stages a newer release, and applies it after normal app exit or before the next launch. Calls and calling apps holding the camera DLL defer installation. Saved cards and annotations remain in their existing local store.

## Local build

Generate the update signing identity once with `./scripts/new-update-key.ps1`. Keep `.local/release-keys/private.json` private, back it up, and reuse it for every tester release. Its matching public key is embedded by packaging. Never regenerate a key just to publish the next version.

```powershell
.\scripts\build.ps1 -ReleaseBuild -Version 0.3.0 -ServiceUrl 'https://callayers-v2-api-3fj7uh2a7a-uc.a.run.app'
ctest --test-dir build/release-0.3.0-x64 -C Release --output-on-failure
.\tests\release-update-test.ps1
.\scripts\package-desktop.ps1 -Version 0.3.0 -PublicKeyFile .local/release-keys/public.json
.\scripts\sign-desktop-release.ps1 -Version 0.3.0 -Installer build/installers/0.3.0/CallayersSetup-0.3.0.exe -PrivateKeyFile .local/release-keys/private.json
```

The installer includes the Microsoft-signed Windows App Runtime 1.6.9 redistributable and app-local Visual C++ runtime files. It uses the same pinned runtime/SDK as the accepted app. The compiler is checksum-pinned Inno Setup 6.4.3, whose license permits commercial use. Runtime installation is attempted for the current user. An access-denied result may request administrator approval for that Microsoft prerequisite only; denial defers installation. Neither the app nor camera registration runs as another administrator account. Install into `%LOCALAPPDATA%/Programs/Callayers`. The installer closes no calling apps, blocks while the existing app/camera is in use, registers the new camera DLL and atomically selects its version. Uninstall preserves the saved workspace. Installation and automatic update acceptance remain user-run.

## GitHub configuration

The deployment and maintenance workflows live in `.github/workflows`:

- Backend tests run for relevant pull requests. Relevant merges to `main` deploy the existing Cloud Run service and validate public health/config/access. Runtime settings/secrets/capacity remain independent. Failure leaves a failed Actions run; rollback uses existing Cloud Run traffic controls.
- Desktop CI builds all accepted MVP features and runs native and update-authentication tests. It does not publish an installer.
- Website checks/builds run for website changes; passing changes on `main` deploy `website/dist` to the direct-upload Cloudflare Pages project `callayers`.
- **Publish desktop tester release** is `workflow_dispatch` only, on `main`. Supply a new numeric version. The workflow tests, packages, signs the update manifest, retains the installer as an Actions artifact, then publishes to Google Cloud Storage. Versioned installer bytes are immutable. The update feed is published only after the public installer passes its checksum check. Publishing older versions over a newer feed is rejected.
- **Clean up desktop releases** runs daily and can be triggered manually. It keeps the latest published installer and the immediately previous published installer. Older versions have a 24-hour grace period starting when they leave that pair; they are removed on the next cleanup after the deadline. `tester/retention.json` records the protected pair and retirement deadlines. Cleanup and publication share a concurrency group. Failed/mismatched metadata stops cleanup; other bucket files and unpublished newer installers are not deleted. Manual runs default to a read-only preview. The Actions installer artifact follows GitHub's separate artifact retention policy.

Configure repository variables `GCP_WORKLOAD_IDENTITY_PROVIDER`, `GCP_DEPLOY_SERVICE_ACCOUNT`, `GCP_RELEASE_SERVICE_ACCOUNT`, and `CLOUDFLARE_ACCOUNT_ID`; configure repository secrets `CLOUDFLARE_API_TOKEN` (website only) and `UPDATE_SIGNING_KEY` (the exact contents of the local private JSON, supplied through GitHub's secret interface or CLI stdin). Release publishing/cleanup use keyless Google authentication. Do not put secrets into source files or command arguments. Restrict access to release workflow execution and protect `main`.

`./scripts/setup-backend-ci.ps1` prepares a dedicated Google deployment service account and keyless identity federation restricted by numeric repository/owner IDs and branch `main`. It saves the two public variable values to `.local/backend-ci.json`. It requires an authorized Google Cloud login; it does not deploy the backend or change its runtime settings.

## Cloudflare configuration

Use an API token scoped to the selected account for Pages edit, plus DNS edit/zone read for initial website domain setup. Initial setup and runtime publishing tokens may be separate, with DNS rights omitted from the publishing token. No R2 access is needed.

The website is already deployed at `callayers.com`. If website provisioning needs repeating, set `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` in the current process, then run `node scripts/setup-cloudflare.mjs`. This prepares the direct-upload Pages project and website domain only. Existing conflicting website DNS is reported without replacing it.

## Google Cloud Storage configuration

The public bucket is `callayers-releases-955736732096` in project `callayers`, single region `us-central1`, Standard storage. The shared public bucket configuration lives in `release/download-config.json` and is included in the installer and website build. Download URLs use `https://storage.googleapis.com/callayers-releases-955736732096/`; no custom-domain load balancer is required. Desktop downloads validate the exact bucket and paths, prohibit redirects, and verify signed metadata and installer hashes.

Create/configure the dedicated bucket through the Google console or, when account changes are authorized, run `./scripts/setup-release-storage.ps1`. It configures uniform bucket access, public release reads (`allUsers`, Storage Object Viewer), website GET/HEAD CORS from `release/gcs-cors.json`, and disables object versioning and soft delete so cleanup does not retain hidden copies. It does not publish an installer. These settings apply only to the dedicated release bucket; never put private files there.

After the separately approved repository/main workload identity exists, `./scripts/setup-release-storage.ps1 -ConfigureCi` also prepares `callayers-release-ci@callayers.iam.gserviceaccount.com`, allows the repository identity to use it, and grants Storage Object Admin on this bucket only. Set GitHub variable `GCP_RELEASE_SERVICE_ACCOUNT` to that email. The script does not create the backend workload identity or grant backend deployment roles. Production IAM approval remains a separate activation step.

The publisher uses Google's JSON API with object-generation preconditions to prevent concurrent overwrites/deletes. Installers use immutable version URLs; the feed and retention record use `no-store`. The website reads that feed for its download button and shows an unpublished state until the feed is available. CORS and anonymous access must be configured before publication.

For local publication after credentials are configured, use `node scripts/publish-desktop.mjs 0.3.0`. This is a real tester release publication, not a dry run. Website deployment is independent of release publication.

Preview retention with `node scripts/cleanup-desktop-releases.mjs --dry-run`; omit `--dry-run` to remove expired retired installers. Publication also cleans up already-expired installers after publishing the feed. The daily workflow handles grace-period expiry even when no further version is released. Run `node --test tests/release-retention-test.mjs` for the isolated retention checks.

## Website preview

```powershell
node website/build.mjs
node website/serve.mjs
```

Open `http://127.0.0.1:4173`. Website source stays in `website/public`; build output is ignored. The source contains a home/download page and a tester privacy page. Its download control follows the published tester feed; it never creates an installer release.

Website credentials, the GCS bucket/public reads/CORS, Google CI identities, and GitHub variable/secret activation are required before hosted delivery can be verified. Prepared builds are not runtime acceptance; step 10 remains open until the user accepts installed behavior and hosted delivery.
