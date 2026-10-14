# Callayers v2

Sign in, choose a webcam, create interactive cards, annotate, and show the result in a call.

## Read the specs

- [APP.md](APP.md): user experience.
- [AI.md](AI.md): generation, revision and deferred website ideas.
- [DATA.md](DATA.md): local saving, state and allowances.
- [BACKEND.md](BACKEND.md): API and deployment.
- [FRAMES.md](FRAMES.md): rendering, input and video delivery.

These define the target product. This is a fresh implementation; v1 is a design reference, not code to restore or port.

## Consolidated plan — session handoff

**This is the master MVP plan.** Overall order, status and the next step live here. [AI implementation](AI-IMPLEMENTATION.md) breaks down the AI milestone without changing this order. Other specs define behavior; implementation notes describe existing code.

### Current position

**Steps 1–9 are done and verified by explicit user acceptance, October 14, 2026. Next is step 10: Runnable MVP release.** The user confirmed Restore previous version and persistence after restart, then accepted the image-preservation and cancelled/outdated-edit checks and stated: "yes, done. all the steps through 9 are done and verified". The existing saved-library implementation is accepted for this MVP; the previously planned per-card storage migration is no longer a gate before release. This status update changes no implementation or storage layout.

Telemetry runtime acceptance completed October 14, 2026: the user performed the three checks and confirmed cards reached Meet. PostHog confirmed session starts, preview card transitions, image import and a newly completed annotation. After a full Brave restart and repeated camera check, PostHog also confirmed Brave identity, connection/output start and stop, and card application/removal in output. The backend is deployed/configured for PostHog Default project 660809. Nine desktop checks and 29 backend checks pass. Consumer identity and camera/card timestamps remain best effort, with no telemetry-off setting or exact duration subsystem.

Updated October 14, 2026. The video foundation, resource investigation, WinUI workspace, remembered webcam, annotations (including text-editing fixes), proportional card scaling and Google sign-in are accepted for their tested builds. Ultralight CPU integration, fixed interactive cards, AI creation, the saved library, AI revision/restoration and MVP telemetry are accepted. GPU composition remains optional and is not implemented. Website tools are deferred beyond the MVP.

**Library/revision status: accepted and closed by the user.** Image import, search, visibility, proportional placement, saving/restoration, compact sidebar, inline renaming and shared Create/Edit chat are accepted. Edit, previous-version restoration after restart, image preservation and cancelled/outdated revision checks are accepted. Build 1b (picture quality/private editing/idle-resume), build 2 (input/positioning/saved values/annotation priority) and build 3 (AI creation/restoration/cancellation/styling/configuration) remain accepted. User-run matched samples showed 44.6–46.7% less private memory for the isolated Ultralight CPU workspace, with lower CPU; evidence lives in [AI implementation](AI-IMPLEMENTATION.md). The user requested replacing the private WebView2 editor with Ultralight before release. The user accepted the isolated `build/text-editor-x64` editor build on October 14, 2026, explicitly stating "done, verified" after the guided multiline-text/Ctrl+Enter check. Ten automated checks passed. Tested executable SHA256: `2E301DB30147091C48CEDEFD0CB28CC7ADE6E38D69F275F8B533CCADEB15B57C`. Accepted steps 1–9 remain closed. Text-editor memory savings remain unmeasured. Memory is the priority; GPU rendering is optional and should be selected only if it lowers RAM. Website reading remains outside the MVP. No completion date is set.

### Complete MVP sequence

| Step | Outcome | Status |
| --- | --- | --- |
| 1. Video foundation and resource baseline | Webcam, cards and annotations reach Meet; minimized calls, idle shutdown, recovery and unavailable output work. Preserve the recorded baseline. | Accepted |
| 2. WinUI workspace | Header, library sidebar, preview, camera help and proportional card scaling. | Accepted |
| 3. Remembered webcam | Reopen the same working device; offer Retry/selection without silently substituting another. | Accepted |
| 4. Annotations | Pen/shapes/text, selection, resizing, eraser, clear and autosave; editing stays private. | Accepted |
| 5. Google sign-in | Browser return, remembered session, account validation and sign-out stopping output while retaining local work. | Accepted |
| 6. Interactive AI cards | Memory-focused HTML rendering/composition, direct HTML interaction, saved state, AI creation and allowances. Ultralight CPU integration accepted; fixed interactive input/state is accepted. AI creation, generated-card restoration/cancellation, independent runtime configuration and glass styling are accepted. The user explicitly closed this milestone. Website tools are deferred beyond the MVP. Evidence lives in [AI implementation](AI-IMPLEMENTATION.md). | Done |
| 7. Image import and saved library | Local PNG/JPEG/BMP, search, visibility, proportional placement, saving/restoration and library management. Compact sidebar, inline renaming and shared Create/Edit chat accepted. Existing storage accepted for the MVP; step explicitly closed October 14, 2026. | Done and verified |
| 8. AI revision and previous version | Edit, referenced-image preservation, previous-version restoration and persistence after restart, cancellation and outdated-response protection. User accepted the remaining checks and explicitly closed this step October 14, 2026. See [AI implementation](AI-IMPLEMENTATION.md#revision-in-step-8). | Done and verified |
| 9. Telemetry and consumer identity | Authenticated sessions, consuming-app identity, best-effort connect/disconnect and card application/removal timestamps, successful creation events. No telemetry-off setting or exact duration subsystem. Basic runtime checks accepted October 14, 2026, including Brave after a full restart. | Done and verified |
| 10. Runnable MVP release | Unsigned per-user installer, automatically staged/authenticated updates, independent backend/website CI and explicitly triggered desktop release workflows are implemented. Local installer 0.3.0 is prepared. Hosted activation, clean-profile installation/sign-in, integrated demo and delivered automatic-update acceptance remain pending. | In progress |

Deliver step 10 through small improvements with short verification in chat. Steps 1–9 are complete by explicit user acceptance; no additional closing checks remain for them. Next: prepare the release installer, camera registration/prerequisites/backend address, then verify Google branding/external-account sign-in on a clean Windows profile and the integrated demo. Fix reported regressions in the affected feature without reopening a completed milestone. Lower resource cost is a measured goal, not an assumed GPU benefit. Preserve accepted behavior throughout.

Website reading is retained as future work: one public page's text, favicon and styling cues, without page-image or arbitrary-logo downloads. It is not part of the MVP plan; see [AI.md](AI.md#website-reading-deferred-beyond-mvp). Billing/trials, cloud sync, remote participant interaction and other explicitly excluded features also remain outside this MVP.

### Step 10 release decisions

Implementation prepared October 14, 2026. [Release operations](../release/README.md) describes building, signing update manifests and activating hosted workflows. The installer and native launcher compile; ten desktop checks, 29 backend checks and 19 update authenticity/deferral checks pass. Windows PowerShell 5.1 and the newer shell both pass update checks. Workflow syntax validation passes. Installer runtime and hosted delivery remain user-verification/activation work. Google CI permission setup is awaiting explicit approval after automatic approval review rejected the production IAM grants; Cloudflare credentials are not available in this session. No deployment authority or hosted publication was applied by that rejected setup.

- First distribution is a small tester release, installed for the current Windows user.
- Backend changes merged into `main` trigger deployment to the existing live service after backend tests pass. Include changes to backend build/deployment inputs when selecting affected changes. Preserve runtime settings, secrets, permissions and capacity; keep released desktop versions compatible. Post-deploy health/readiness and unsigned-access checks report failures. Start with visible failure notification and manual rollback.
- Desktop CI builds and runs existing checks; desktop release publication is explicitly triggered, not automatic on each merge.
- Keep the latest and immediately previous published desktop installer in R2. Older releases receive a 24-hour grace period before daily cleanup; publication remains explicitly triggered. Cleanup protects the published pair and stops if release metadata disagrees.
- The user owns `callayers.com`, uses Cloudflare and has nothing deployed yet. Keep website source in a `website/` folder in this monorepo and configure its own CI/deployment flow: relevant website changes merged to `main` deploy the site after checks pass, independently of backend deployments and explicitly triggered desktop releases. Proposed hosting: Cloudflare Pages for the site and R2 behind a downloads subdomain for installer artifacts/update feeds. Actual resources, publishing credentials and HTTPS configuration remain to be established. Use versioned installer URLs and a separately published tester update feed; publish the feed only after its referenced artifact is available. Website deployments must not implicitly publish a desktop release.
- The user explicitly chose an unsigned installer for the first tester release; no Windows signing identity/service is currently available. Confirm downloaded installation behavior on clean tester PCs. Windows reputation warnings or policy blocks are possible. Windows certificate provisioning remains separate from updater signature keys.
- The user chose automatic desktop installation. Check/download updates automatically, verify update authenticity, and apply them at the next safe exit/start without a routine Install and restart confirmation. Avoid interrupting camera output or force-closing calling apps; account for calling apps retaining the camera DLL. Preserve saved work and keep a recovery download available. The exact updater remains to be selected.
- Routine per-user updates should not require administrator rights: the current camera DLL registers its COM class and DirectShow category only under `HKEY_CURRENT_USER/Software/Classes`. Keep installed app files in a user-writable location and registration running as the intended user. If a prerequisite or future update genuinely requires elevation, explain the reason before asking Windows for administrator approval. If approval is unavailable or declined, defer the update and keep the working version. Prompt for a calling-app restart only when needed; a DLL in use is distinct from an administrator permission requirement. Windows may still require interaction for unsigned software; automatic installation must not assume it can suppress platform prompts. Confirm the complete installation/update behavior on a standard-user clean profile.
- The implemented updater uses a short-lived Windows PowerShell helper and built-in RSA/SHA-256 signature verification for bounded release metadata plus installer hashes. It adds no resident service or third-party updater runtime. Only newer tester releases from the fixed HTTPS download host can be applied; signed metadata and staged installer bytes are checked again immediately before execution. WinSparkle remains an alternative considered during planning. Windows publisher signing remains separate from the application-owned update signing identity.

Planning references: [Microsoft SmartScreen reputation](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation), [MSIX distribution requirements](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/choose-distribution-path), and [WinSparkle update signature documentation](https://winsparkle.org/guides/getting-started/).

Hosting references: [Cloudflare Pages monorepo configuration](https://developers.cloudflare.com/pages/configuration/monorepos/), [Pages build watch paths](https://developers.cloudflare.com/pages/configuration/build-watch-paths/), and [R2 public buckets/custom domains](https://developers.cloudflare.com/r2/buckets/public-buckets/).

### How we build and verify

Follow [AGENTS.md](../AGENTS.md#feature-verification). Prepare builds and put short verification steps in chat; the user reports runtime results. Record accepted outcomes and tested builds in Git. Do not create verification documents. The assistant may stop this workspace's development app to rebuild after giving notice.

### Accepted evidence

Acceptance below was recorded October 13, 2026 unless a later date is shown; details remain in Git history and conversation. SHA256 identifies the tested executable, not the current target specification.

| Accepted outcome | Tested executable SHA256 / backend |
| --- | --- |
| Steps 1–9 explicitly accepted; restoration/restart, image preservation and cancelled/outdated edits; basic telemetry, October 14, 2026 | Desktop `F1C7F047EA7654D94283CB2C98A23E81F249CEEC30C4A2E4D8CEABACB11BDDF1`; camera `7DD3CF2CD5AD3D0916B4DE50043F5EADED7D420A70FD90A2CED5B8404FB1EC31`; deployed telemetry backend `callayers-v2-api-00014-29l` |
| Compact sidebar verified | `C67FB6F293071CFD45F160BBB6870E31E3CA10C2B09A583C6B1C2584A1EA95B8` |
| Edit working after user-deployed revision backend (sidebar build) | `A9F698AEE7E09A1BF0F399CC15F758BD1D8C339A311DBC2BF41415917593B0B7` |
| Image import, search, visibility, proportional placement and restoration | `FA7D3F4CD387BA1B2A74753ABB1D84698E2D7052F0586A6D7A902CC38DBF4D32` |
| Workspace/video walkthrough | `54EB0E3B14251D1B920C9D055E10D0285A180D26AA89D9CE16EEF2FDE63E46C4` |
| Proportional card scaling | `4214D343D03FA5C191FD69F14601693EF2F54052DAE93E2AF95C1CAE6CEF6B5E` |
| Remembered webcam | `C699F4E235B70289D35DECCE52E1C3EEB8A85757E7741D79A169AC86594DB447` |
| Sign-in, session restoration and sign-out gating | `6E30628CEFE469663EF265D95668982EFCD149EF0B05A5E9612928E41124759C`; `callayers-v2-api-00001-z56` |

The user accepted the observed resource cost, without an optimization claim. Matched samples measured about 9.54 extra whole-machine CPU percentage points with the 1080p webcam and 5.27 with 720p; minimized idle was 0% CPU / 154 MiB, and quit left no tracked processes. The later 30.37-second workspace sample `20261013-145411-MinimizedMeet-08d85b-summary.json` measured Callayers 10.197% CPU / 306.91 MiB, Brave 4.361% / 3570.65 MiB, with no warnings; no matched raw-camera sample accompanied it. These are setup-specific observations, not universal limits.

Google Audience is External and publishing is In production, confirmed by the user. Branding/verification and the external-account/clean-profile check remain step 10.

## Reference files

- [Screenshot](references/ui.png) and [annotation bar](references/annotation-bar.png): visual references.
- `assets/`: logo files. [Old WinUI shell](references/winui_shell.h): styling reference only.
- V1 reference commit: `384e9078d3864d16332178c5cb72776b998ee46d`; pre-cloud baseline: `634e4c3`.
