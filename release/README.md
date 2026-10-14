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

Four independent workflows live in `.github/workflows`:

- Backend tests run for relevant pull requests. Relevant merges to `main` deploy the existing Cloud Run service and validate public health/config/access. Runtime settings/secrets/capacity remain independent. Failure leaves a failed Actions run; rollback uses existing Cloud Run traffic controls.
- Desktop CI builds all accepted MVP features and runs native and update-authentication tests. It does not publish an installer.
- Website checks/builds run for website changes; passing changes on `main` deploy `website/dist` to the direct-upload Cloudflare Pages project `callayers`.
- **Publish desktop tester release** is `workflow_dispatch` only, on `main`. Supply a new numeric version. The workflow tests, packages, signs the update manifest, retains the installer as an Actions artifact, then publishes to R2. Versioned installer bytes are immutable. The update feed is published only after the public installer passes its checksum check. Publishing older versions over a newer feed is rejected.

Configure repository variables `GCP_WORKLOAD_IDENTITY_PROVIDER`, `GCP_DEPLOY_SERVICE_ACCOUNT`, and `CLOUDFLARE_ACCOUNT_ID`; configure repository secrets `CLOUDFLARE_API_TOKEN` and `UPDATE_SIGNING_KEY` (the exact contents of the local private JSON, supplied through GitHub's secret interface or CLI stdin). Do not put secrets into source files or command arguments. Restrict access to release workflow execution and protect `main`.

`./scripts/setup-backend-ci.ps1` prepares a dedicated Google deployment service account and keyless identity federation restricted by numeric repository/owner IDs and branch `main`. It saves the two public variable values to `.local/backend-ci.json`. It requires an authorized Google Cloud login; it does not deploy the backend or change its runtime settings.

## Cloudflare configuration

Use an API token scoped to the selected account for Pages edit and R2 object/storage edit, plus DNS edit/zone read for initial domain setup. Initial setup and runtime publishing tokens may be separate, with DNS rights omitted from the publishing token.

Set `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` in the current process, then run `node scripts/setup-cloudflare.mjs`. This prepares a direct-upload Pages project, associates `callayers.com`, creates `callayers-downloads`, associates `downloads.callayers.com`, and configures website read CORS. Existing conflicting website DNS is reported without replacing it. R2 must be enabled in the account. Wait for the custom domains' certificates to become active before publishing.

The release workflow uses checksum-verified release content and pinned Wrangler 4.77.0 to store installer content types and cache metadata. Installers use immutable version URLs; `tester/latest.json` uses `no-store`. Avoid adding a Cloudflare cache rule that overrides that feed policy. The website reads that feed for its download button and shows an unpublished state until the feed is available.

For local publication after credentials are configured, use `node scripts/publish-desktop.mjs 0.3.0`. This is a real tester release publication, not a dry run. Website deployment is independent of release publication.

## Website preview

```powershell
node website/build.mjs
node website/serve.mjs
```

Open `http://127.0.0.1:4173`. Website source stays in `website/public`; build output is ignored. The source contains a home/download page and a tester privacy page. Its download control follows the published tester feed; it never creates an installer release.

Cloudflare credentials and GitHub variable/secret activation are required before hosted CI and update delivery can be verified. Prepared builds are not runtime acceptance; step 10 remains open until the user accepts installed behavior and hosted delivery.
