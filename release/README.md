# Tester release operations

Step 10 implementation uses an unsigned per-user Inno installer, versioned app directories, Windows PowerShell 5.1 for a short-lived updater, and RSA/SHA-256 authenticated release manifests. Windows publisher signing is separate. The updater checks at app startup, stages a newer release, and applies it after normal app exit or before the next launch. Calls and calling apps holding the camera DLL defer installation. Saved cards and annotations remain in their existing local store.

File → About Callayers shows the running build's version and one update button. Installed releases support Check for updates, checking/downloading status, and Restart to update after an authenticated download is ready. Restart asks users to close calling apps holding the camera first, then saves through the normal quit path, applies the update, and reopens Callayers. Development builds show their version with updates disabled.

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

- **Backend — Test and deploy** tests relevant pull requests. Relevant merges to `main` deploy the existing Cloud Run service and validate public health/config/access. Manual runs are available for rerunning tests/deployment. Runtime settings/secrets/capacity remain independent. Failure leaves a failed Actions run; rollback uses existing Cloud Run traffic controls.
- **Windows app — Build and test** builds all accepted MVP features and runs native and update-authentication tests on relevant pull requests and merges to `main`. It does not publish an installer.
- **Website — Build and deploy** checks/builds website changes; passing changes on `main` deploy `website/dist` to the direct-upload Cloudflare Pages project `callayers`. Manual runs are available for rebuilding/redeploying the website.
- **Windows app — Publish new version** is manual only (`workflow_dispatch`), on `main`. Supply a new numeric version. The workflow tests, packages, signs the update manifest, retains the installer as an Actions artifact, then publishes to Cloudflare R2. Versioned installer bytes are immutable. The update feed is published only after the public installer passes its checksum check. Publishing older versions over a newer feed is rejected.
- **Windows app — Clean up old downloads** runs daily and can be triggered manually. It keeps the latest published installer and the immediately previous published installer. Older versions have a 24-hour grace period starting when they leave that pair; they are removed on the next cleanup after the deadline. `tester/retention.json` records the protected pair and retirement deadlines. Cleanup and publication share a concurrency group. Failed/mismatched metadata stops cleanup; other bucket files and unpublished newer installers are not deleted. Manual runs default to a read-only preview. The Actions installer artifact follows GitHub's separate artifact retention policy.

Configure repository variables `GCP_WORKLOAD_IDENTITY_PROVIDER` and `GCP_DEPLOY_SERVICE_ACCOUNT` for backend CI, and `CLOUDFLARE_ACCOUNT_ID` for website/releases. Configure repository secrets `CLOUDFLARE_API_TOKEN` (website Pages publishing), `CLOUDFLARE_R2_API_TOKEN` (release publishing/cleanup), and `UPDATE_SIGNING_KEY` (the exact contents of the local private JSON, supplied through GitHub's secret interface or CLI stdin). Desktop releases do not use Google CI credentials. Do not put secrets into source files or command arguments. Restrict access to release workflow execution and protect `main`.

`./scripts/setup-backend-ci.ps1` prepares a dedicated Google deployment service account and keyless identity federation restricted by numeric repository/owner IDs and branch `main`. It saves the two public variable values to `.local/backend-ci.json`. It requires an authorized Google Cloud login; it does not deploy the backend or change its runtime settings.

## Cloudflare configuration

Use the website token for Pages edit, with DNS edit/zone read only for initial website domain setup. Use a separate release token with Workers R2 Storage edit for the selected account. Initial download-domain setup also needs the relevant zone permissions; the runtime release token need not edit website DNS.

The website is already deployed at `callayers.com`. If website provisioning needs repeating, set `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` in the current process, then run `node scripts/setup-cloudflare.mjs`. This prepares the direct-upload Pages project and website domain only. Existing conflicting website DNS is reported without replacing it.

## R2 download configuration

Enable R2 in the Cloudflare account, create the Standard storage bucket `callayers-downloads`, connect the custom domain `downloads.callayers.com`, and configure website GET/HEAD CORS. The shared public bucket/address configuration lives in `release/download-config.json` and is included in the installer and website build. Desktop downloads validate the exact HTTPS host and paths, prohibit redirects, and verify signed metadata and installer hashes.

Configure the bucket/domain/CORS through the Cloudflare dashboard or, with authorized credentials, run `node scripts/setup-release-storage.mjs`. It configures only R2 downloads, independently of the existing website. The CORS rule allows origin `https://callayers.com`, methods GET/HEAD, and a 3600-second preflight lifetime. Wait for the custom domain and certificate to become active before publication. The bucket contains public release files only.

For local setup/publication, supply `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` through environment variables. GitHub maps its separate `CLOUDFLARE_R2_API_TOKEN` secret to the same process variable. Never paste a token into chat. The backend Google identity setup remains separate.

The publisher/cleanup use the Cloudflare REST API for reads/listing/deletion and pinned Wrangler 4.77.0 for uploads with stored content/cache metadata. Publication and cleanup share the GitHub concurrency group; local operators must serialize publication/cleanup too. Preflight reads detect externally changed metadata and existing version bytes. Installers use immutable version URLs; the feed and retention record use `no-store`. Do not override that feed policy with a Cloudflare cache rule. The website shows an unpublished state until the feed is available.

The GCS trial bucket `callayers-releases-955736732096` was created privately. Public access was blocked by `iam.managed.allowedPolicyMembers`; the user chose R2 instead. Its organization policy was not changed and the bucket is not used by releases.

For local publication after credentials are configured, use `node scripts/publish-desktop.mjs 0.3.0`. This is a real tester release publication, not a dry run. Website deployment is independent of release publication.

Preview retention with `node scripts/cleanup-desktop-releases.mjs --dry-run`; omit `--dry-run` to remove expired retired installers. Publication also cleans up already-expired installers after publishing the feed. The daily workflow handles grace-period expiry even when no further version is released. Run `node --test tests/release-retention-test.mjs` for the isolated retention checks.

## Website preview

```powershell
node website/build.mjs
node website/serve.mjs
```

Open `http://127.0.0.1:4173`. Website source stays in `website/public`; build output is ignored. The source contains a home/download page and a tester privacy page. Its download control follows the published tester feed; it never creates an installer release.

Website/release credentials, R2 bucket/domain/CORS activation, and GitHub variable/secret activation are required before hosted delivery can be verified. Prepared builds are not runtime acceptance; step 10 remains open until the user accepts installed behavior and hosted delivery.


## Linux beta

The first Linux beta supports **Ubuntu 26.04 x64 with Secure Boot off**. The application package includes the pinned renderer, uses the existing production backend, and rejects development-only flags. Camera setup uses the host's `v4l2loopback-dkms`; the app is not a portable driver bundle. Windows releases and signing keys remain separate.

**Linux app — Build and test** runs for relevant pull requests, main updates, and manual runs. It builds inside Ubuntu 26.04 on a GitHub Ubuntu runner, tests development interactions and the production sign-in gate, packages the production executable, and checks it after extraction without development library paths. Repository tests use a disposable signing key and real APT signature checks. No kernel module is loaded in hosted CI, so CI does not prove camera installation or reboot behavior.

The artifact `callayers-ubuntu-26.04-amd64` contains the `.deb`, `install.sh`, and `SHA256SUMS`. On a clean Ubuntu 26.04 x64 desktop, extract the artifact, verify `sha256sum --check SHA256SUMS`, then run `sudo bash ./install.sh --deb ./callayers_0.3.2_amd64.deb` (substitute the artifact version). This is the same installer path used for public installation. Use a physical webcam or USB passthrough for a VM. Runtime acceptance is recorded in the conversation, not a separate checklist file.

For a local candidate, run `bash linux/packaging/build-deb.sh 0.3.2`. The output is `build/linux/callayers_0.3.2_amd64.deb`. Bump `CALLAYERS_RELEASE_VERSION` in `linux/CMakeLists.txt` before building the next CI candidate; never reuse a version for different bytes.

### One-time repository setup

Create a dedicated OpenPGP signing identity for the Linux archive, retain a secure backup, and configure its ASCII-armored private key as repository secret `LINUX_APT_SIGNING_KEY`. Configure its full 40-character fingerprint as repository variable `LINUX_APT_SIGNING_FINGERPRINT`. The release runner uses a noninteractive signing key; a passphrase-protected key requires separate passphrase handling, which this workflow does not implement. Never place the key in Git, build artifacts, or chat. The disposable keys generated by tests are not release identities.

Reuse repository secret `CLOUDFLARE_R2_API_TOKEN` and variable `CLOUDFLARE_ACCOUNT_ID` for the existing download bucket. The bucket's website CORS policy must permit reading `linux/apt/dists/stable/InRelease`, just as it permits the Windows feed. No new hosting vendor is required. Keep the archive identity stable: the publisher refuses to replace an existing public key; rotation requires a separately planned migration.

### Publish the tested artifact

After verifying installation, real sign-in, card/camera output in a calling app, and reboot, run **Linux app — Publish tested version** on `main`. Supply the successful Linux build run ID and acknowledge the installation verification. The workflow checks the run belongs to the Linux build workflow on this repository's `main`, downloads that exact artifact, verifies its checksums, and checks out its source commit. It does not rebuild the executable.

The publisher signs APT metadata, uploads immutable versioned packages and content-addressed indices, verifies their public download bytes, then updates `InRelease` as the publication point. Old packages and indices remain available; failed uploads before publication leave the prior signed index in place. Downgrades and replacing an existing package version with different bytes are refused. A corrective release must use a higher version. No Linux retention/deletion automation is enabled for this beta.

The website exposes the Ubuntu installation command only after the published Linux metadata is reachable. Website changes still require the existing website deployment workflow. Customers install through `https://downloads.callayers.com/linux/install.sh`; subsequent updates use the normal APT package manager. Callayers has no separate in-app updater; system update policy controls when APT upgrades run. Users should close Callayers and their calling app before upgrading.

These workflows must be committed and pushed before they appear in GitHub Actions. Local tests do not imply an Actions run or a public release has occurred.
