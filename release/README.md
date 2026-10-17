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
- **Windows app — Publish new version** is manual only (`workflow_dispatch`), on `main`. Choose `patch` (default), `minor`, or `major`. The workflow reads the latest published Windows version, calculates the next version, and displays both in the run summary. It tests, packages, signs the update manifest, retains the installer as an Actions artifact, then publishes to Cloudflare R2. Versioned installer bytes are immutable. The update feed is published only after the public installer passes its checksum check. Publishing older versions over a newer feed is rejected.
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

## Version source of truth

Each platform has its own published version; Windows and Linux need not match. Windows uses the signed envelope at [`tester/latest.json`](https://downloads.callayers.com/tester/latest.json): base64-decode its `payload` and read `version`. Linux uses the signed APT [`linux/apt/dists/stable/InRelease`](https://downloads.callayers.com/linux/apt/dists/stable/InRelease): read `X-Callayers-Version`. Those files become current only at the publication point, after public package verification. Git tags, Actions run numbers, unpublished artifacts, `tester/retention.json`, and source defaults do not advance the current public version. `release/download-config.json` specifies storage locations, not a release version.

`scripts/release-version.mjs` reads the appropriate feed over HTTPS without cache or redirects and calculates the selected increment. Only HTTP 404 permits a bootstrap from the relevant CMake default (`CALLAYERS_VERSION` in root `CMakeLists.txt` or `CALLAYERS_RELEASE_VERSION` in `linux/CMakeLists.txt`), applying the selected increment to that starting point. Other HTTP failures, network errors, malformed metadata, and numeric overflow stop the run. For example, a published `0.3.9` becomes `0.3.10` / `0.4.0` / `1.0.0`; if Linux is unpublished and its CMake default is `0.3.4`, choosing Patch prepares `0.3.5`.

GitHub's launch dropdown lists fixed increment choices; the current and next numeric versions appear in the run summary after it starts. Summaries and `VERSION`/step outputs carry the calculated number into building, signing and artifact naming. Each Linux publish run serializes version selection, building, compatibility testing, and publication under the existing Linux release concurrency group. It selects the latest `main` commit at checkout and pins that commit across the remaining jobs, so later merges cannot change the package during the run. Existing publisher checks continue to prevent downgrades and replacement of immutable bytes. These version reads do not replace installer/update signature verification. The website's download section reads the same feeds and displays each platform's version, `Not published yet` for HTTP 404, or `Release status unavailable` for failed or malformed lookups.

## Website preview

```powershell
node website/build.mjs
node website/serve.mjs
```

Open `http://127.0.0.1:4173`. Website source stays in `website/public`; build output is ignored. The source contains a home/download page and a tester privacy page. Its download control follows the published tester feed; it never creates an installer release.

The download panel selects Windows, Mac, or Linux using best-effort browser platform hints, with a manual platform switch for unsupported/unknown browsers and downloads for another computer. Header and hero buttons link directly to the published Windows installer when Windows is selected; Linux opens/focuses its distribution picker. Linux commands appear only after a distribution is selected: Ubuntu uses the standard installer, and Debian/Mint/Pop!_OS/Fedora/Arch include `--allow-experimental` with distribution-specific requirements. Fedora additionally offers an explicit RPM Fusion Free checkbox; its flag is never added by default. Fedora/Arch commands appear only after the published metadata advertises native repositories. Other distributions expose no install command. Browser detection does not establish Linux distribution, supported release, CPU compatibility, or Secure Boot state; the installer performs the actual machine checks. Successful Linux copies include the selected `distribution` in analytics; direct Windows hero/navigation downloads retain the existing download event with the corresponding button location.

Website/release credentials, R2 bucket/domain/CORS activation, and GitHub variable/secret activation are required before hosted delivery can be verified. Prepared builds are not runtime acceptance; step 10 remains open until the user accepts installed behavior and hosted delivery.


## Linux beta

The Linux beta targets **Ubuntu 22.04, 24.04, and 26.04 x64 with Secure Boot off**. Installation and camera acceptance on each desktop must precede advertising it as verified. The application package includes the pinned renderer, uses the existing production backend, and rejects development-only flags. Camera setup uses distro-maintained `v4l2loopback-dkms` or Fedora's RPM Fusion Free `akmod-v4l2loopback`; the app is not a portable driver bundle. Windows releases and signing keys remain separate.

**Linux app — Build and test** runs for relevant pull requests, main updates, and manual test runs. It is also reusable by **Linux app — Publish new version**, which supplies the calculated version and pinned main commit. Ordinary CI builds use the CMake default and do not publish.

The workflow builds the release package inside Ubuntu 22.04 on a GitHub Ubuntu runner, tests development interactions and the production sign-in gate, packages the production executable, and checks it after extraction without development library paths. Repository tests use a disposable signing key and real APT signature checks. The exact package is also checked for dependency resolution and renderer loading in Ubuntu 24.04/26.04 and Debian 12/13 containers. These checks use the baseline package rather than rebuilding on each distribution. The baseline payload is wrapped in RPM and Arch packages and checked in Fedora 43/44 and current Arch containers. Fedora checks resolve/download the complete dependency set including the RPM Fusion Free driver; Arch checks installed dependency metadata. No Callayers camera hook is run in hosted CI, so CI does not prove camera installation or reboot behavior.

The artifact `callayers-ubuntu-lts-amd64` contains the `.deb`, `install.sh`, and `SHA256SUMS`. On each clean Ubuntu 22.04/24.04/26.04 x64 desktop, extract the artifact, verify `sha256sum --check SHA256SUMS`, then run `sudo bash ./install.sh --deb ./callayers_0.3.3_amd64.deb` (substitute the artifact version). This is the same installer path used for public installation. Use a physical webcam or USB passthrough for a VM. Runtime acceptance is recorded in the conversation, not a separate checklist file.

Build release candidates on Ubuntu 22.04 to retain the older Qt/glibc baseline; building on a newer host derives newer dependency requirements and does not produce the multi-version candidate. For a local candidate, pass an explicit version, e.g. `bash linux/packaging/build-deb.sh 0.3.5`; the output is `build/linux/callayers_0.3.5_amd64.deb`. The artifact `callayers-native-amd64` contains `.rpm` and `.pkg.tar.zst` packages plus checksums. The hosted publish workflow overrides the CMake default automatically; no source version bump is required for each release. Never reuse a version for different bytes. The existing Linux signing identity is reused for all formats; no new signing secret or hosting service is required.

### One-time repository setup

Create a dedicated OpenPGP signing identity for the Linux archive, retain a secure backup, and configure its ASCII-armored private key as repository secret `LINUX_APT_SIGNING_KEY`. Configure its full 40-character fingerprint as repository variable `LINUX_APT_SIGNING_FINGERPRINT`. The release runner uses a noninteractive signing key; a passphrase-protected key requires separate passphrase handling, which this workflow does not implement. Never place the key in Git, build artifacts, or chat. The disposable keys generated by tests are not release identities.

Reuse repository secret `CLOUDFLARE_R2_API_TOKEN` and variable `CLOUDFLARE_ACCOUNT_ID` for the existing download bucket. The bucket's website CORS policy must permit reading `linux/apt/dists/stable/InRelease`, just as it permits the Windows feed. No new hosting vendor is required. Keep the archive identity stable: the publisher refuses to replace an existing public key; rotation requires a separately planned migration.

### Publish from main

For Linux 0.3.4 adoption tracking, deploy the updated backend and website before publishing the app. The backend must accept the new `platform` event field; the website adds successful installation-command copies tagged with the published version. See [Linux adoption event semantics](../backend-v2/README.md#mvp-telemetry) for triggers, account attribution, and funnel filters. A green local build does not confirm live PostHog arrival.

Run **Linux app — Publish new version** on `main` and choose `patch` (default), `minor`, or `major`. No run ID or installation-confirmation input is required. The workflow first checks required signing/storage configuration, selects the latest main commit, reads the current published Linux version, and displays the selected next version. It then calls the shared Linux build/test workflow with that version and commit. Publication waits for the development, production package, repository, and all baseline and native compatibility-container checks to pass. A signing job downloads the baseline/native artifacts from the same run, verifies checksums/versions, signs native packages and repository metadata, and retains the signed repository as `callayers-linux-signed-repository`. A separate publishing job uploads that signed artifact. Rerun failed publication jobs to preserve these exact signed bytes; rerunning successful build/sign jobs may produce different bytes for an already uploaded version. It does not accept an artifact from another run or renumber the package after building.

Publication now follows automated checks directly; there is no separate manual installation checkpoint. Actual camera setup, real Google sign-in, calling-app output, and reboot behavior across desktops remain runtime acceptance work, and hosted container success alone does not establish them.

The publisher validates APT, RPM, and pacman signatures and native index/package checksums before uploading. It uploads immutable package/hash objects first, then the signed native repository heads, verifies public bytes, and publishes APT `InRelease` last. This final marker enables Fedora/Arch website commands. DNF and pacman have their own signed metadata publication heads; a failure late in publication can leave a native repository advanced while the APT/website marker is still old. Inspect the failed run and rerun its failed publishing job before announcing the release. Old packages and indices remain available. APT retains its prior signed index until the final InRelease write. Downgrades and replacing an existing package version with different bytes are refused. A corrective release must use a higher version. No Linux retention/deletion automation is enabled for this beta.

The website exposes the Ubuntu installation command only after the published Linux metadata is reachable. Website changes still require the existing website deployment workflow. Customers install through `https://downloads.callayers.com/linux/install.sh`; subsequent updates use APT, DNF, or pacman for the selected system. Callayers has no separate in-app updater; system update policy controls when APT upgrades run. Users should close Callayers and their calling app before upgrading.

These workflows must be committed and pushed before they appear in GitHub Actions. Local tests do not imply an Actions run or a public release has occurred.

### Experimental Debian, Mint, and Pop!_OS installation

Debian 12/13 and Mint/Pop!_OS releases declaring an Ubuntu jammy, noble, or resolute base can opt in with `--allow-experimental`. This is a compatibility trial, not verified desktop or camera support. Older bases, other distributions, ARM, and Secure Boot remain outside this installer. Linux Mint Debian Edition is not covered by the Ubuntu-base detection.

For a downloaded candidate use `sudo bash ./install.sh --allow-experimental --deb ./callayers_0.3.3_amd64.deb`. Once a release is published, use `curl -fsSL https://downloads.callayers.com/linux/install.sh | sudo bash -s -- --allow-experimental`. The script warns before changing the machine. APT must resolve every dependency, and headers for the running kernel must be available for the camera driver. It installs only the running kernel's headers, avoiding an unrelated Ubuntu kernel metapackage on derivatives or Debian.

### Experimental Fedora and Arch installation

Traditional Fedora 43/44 and Arch Linux x64 opt in through the same installer using `--allow-experimental`. Fedora Atomic/OSTree editions and Arch derivatives are not covered. Real driver loading, installed Google sign-in, calling-app output, and reboot have not been accepted on these desktops.

Fedora requires RPM Fusion Free for its camera driver. The installer never enables it silently: users either configure it beforehand or add `--enable-rpmfusion`. That explicit flag prints an explanation, validates the pinned RPM Fusion Free key fingerprint, and verifies the community repository release package. Only the Free repository is enabled. The Callayers RPM repository is `linux/rpm`, with signed `repodata/repomd.xml` and signed RPM packages.

Arch installation performs a full upgrade, installs the matching kernel headers and `v4l2loopback-dkms`, and can require reboot/rerun if the running kernel no longer has matching headers. The Callayers repository is `linux/arch/x86_64`, with signed packages and signed pacman databases; the installer does not disable signature verification or install an AUR helper.

Before hosted publication, install local candidates with `sudo bash ./install.sh --allow-experimental --package ./callayers-<version>-1.x86_64.rpm` (Fedora) or `sudo bash ./install.sh --allow-experimental --package ./callayers-<version>-1-x86_64.pkg.tar.zst` (Arch). Add the explicit Fedora repository flag only when desired. Local candidate mode does not configure Callayers repository updates; rerun the normal public installer after release.

## Mac build and release preparation

The native Mac product is `Callayers.app`; the stable bundle IDs remain `com.callayers.renderer-preview` and `com.callayers.renderer-preview.camera` so existing camera approvals and producer authentication remain valid. Intel and Apple Silicon use separate archives and feeds. The app uses pinned Sparkle 2.9.6, validates signed feeds and archives before extraction, and defers installation during camera use or unfinished AI/import work. Release startup checks the bundled extension version and requests an upgrade only while no calling app is consuming the camera; macOS can still require approval or restart. Development builds do not check the public update feed or automatically replace the installed camera.

A development checkpoint can be prepared with:

```sh
bash scripts/package-macos.sh --development --arch x86_64 --team TEAM_ID
```

Use `--arch arm64` for the Silicon artifact; building it on Intel does not run its renderer or establish physical Mac acceptance. `--skip-build` packages an already verified development build. Archives contain just the application, with a separate SHA-256 file, and existing versioned archives are never overwritten. `check-macos-package.py` verifies binary architecture, bundled dependencies/resources and signatures. `macos/Tests/check-update-signing.py` exercises the actual pinned Sparkle signer using a disposable private key; it does not install an update.

Public release preparation requires a **Developer ID Application** certificate with its private key, provisioning for the host and camera extension, a notarization credential saved under a named `notarytool` Keychain profile, and a durable Ed25519 update key. Apple Development signing alone is insufficient. Keep the update key out of Git and build artifacts, and back it up before publication. Generate it once in a protected, ignored local directory; the helper refuses to replace an existing file and prints only its public key:

```sh
mkdir -p .local/release-keys
xcrun swift scripts/macos-update-key.swift --generate .local/release-keys/macos-ed25519.key
```

Prepare each architecture in its own fresh output directory:

```sh
bash scripts/package-macos.sh --arch arm64 --team TEAM_ID \
  --identity 'Developer ID Application: YOUR NAME (TEAM_ID)' \
  --notary-profile CallayersNotary \
  --ed-key-file .local/release-keys/macos-ed25519.key \
  --output build/mac-release-arm64
```

The release path archives and exports with Developer ID signing and hardened runtime, checks the app, submits to Apple's notary service, requires acceptance, staples/validates the ticket, checks Gatekeeper, then creates the final ZIP and signed Sparkle appcast. It preserves failed outputs for diagnosis and never publishes or changes `/Applications`. The script has not completed a real Developer ID export/notarization on the initial Intel machine; the needed distribution identity is not installed.

Publication remains a separate operation after the chosen release's runtime acceptance. For the initial October 17 beta, the user explicitly authorizes both architectures after Intel installation verification, with the Apple silicon download labeled experimental and untested on Apple silicon hardware. Installed automatic upgrades and physical Apple silicon execution remain unverified for this beta. Immutable archives belong at `https://downloads.callayers.com/macos/releases/<version>/<arch>/`; publish the matching signed feed at `https://downloads.callayers.com/macos/<arch>/appcast.xml` only after public archive verification. Never reuse a version/build number for different public bytes. The first feed and subsequent upgrades need the same retained signing identity. Keep prior releases for recovery. Later upgrade acceptance covers saved work, an active-call deferral, extension activation/restart handling, and both processor families.

Mac website availability is prepared independently for each architecture. Production packaging writes `feed/latest.json` only after notarization, stapling, and Gatekeeper assessment succeed. Publish its versioned ZIP first, then `macos/<arch>/appcast.xml` and `macos/<arch>/latest.json` last; permit HTTPS cross-origin reads for the availability JSON. The website offers explicit Apple silicon and Intel choices and never infers the CPU from `MacIntel` browser strings. Development ZIPs do not generate availability metadata and must not be published through these release paths. No Mac artifacts or website changes were published during local preparation.

Validate the final prepared release without storage credentials or publication:

```sh
node scripts/publish-macos.mjs build/mac-release-arm64 --arch arm64 --dry-run
```

Use the corresponding output directory and `--arch x86_64` for Intel. The publisher reads the public update key from the final ZIP and checks the signed appcast and enclosure, notarization result, extracted bundle signatures, stapled ticket, and Gatekeeper. After runtime acceptance, omit `--dry-run` to publish using the existing `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` environment configuration. It permits only the Mac object namespace, refuses changed archives or reused builds, verifies exact anonymous public ZIP/checksum bytes before advancing the appcast, and publishes website availability last. The publisher does not need the private update key. Architectures publish separately and a failed partial upload can be retried with the same prepared release. Website deployment remains separate. Run `node --test tests/macos-publish-test.mjs tests/release-retention-test.mjs` for isolated Mac/Windows publishing checks; Linux repository fixtures require their Linux toolchain.
