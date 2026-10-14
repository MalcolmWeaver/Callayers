# Callayers v2

Sign in, choose a webcam, create cards, annotate, and show the result in a call.

## Read the specs

1. [The app](APP.md) — user flow, layout, and behavior.
2. [Creating cards with AI](AI.md) — what the backend generates.
3. [What gets saved](DATA.md) — local data, database, and settings.
4. [Backend](BACKEND.md) — endpoints, telemetry, and deployment.
5. [Video: MVP and architecture](FRAMES.md) — video design, build order, and minimum verification.

These specs describe a fresh implementation. Git history is reference material and prior evidence, not code to restore or port.

## Consolidated plan — session handoff

Goal: a usable MVP that meets [APP.md](APP.md), delivered through small improvements a person can understand and verify. This is the consolidated delivery order; detailed behavior remains in the linked specs. No remaining-work completion date has been established.

### Current position

The WinUI workspace, remembered webcam, and full annotation workflow are accepted. On October 13, 2026, the user also verified and completed the text-editing improvements: matching editing/completed formatting, the cyan editing outline, and immediate newline rendering. On October 13, 2026, the user verified all three integrated Google sign-in checks: browser sign-in and return to the working webcam, remembered sign-in and annotations after restarting, and File → Sign out stopping live Meet output while preserving annotations for the next sign-in. Google sign-in and remembered sessions are accepted. Next is AI card creation, allowance enforcement and local saving. The user confirmed Google Auth Platform Audience is External and publishing status is In production. Branding/verification status and an external-user/clean-profile check remain release-readiness items.

The user completed the video-foundation walkthrough: webcam/cards/annotations reach Meet, editing handles stay private, minimized calls continue, idle capture stops, reconnection and camera Retry work, and normal quit supplies unavailable output and releases the tracked processes. Preserve the working video engine. On October 13, 2026, the user reviewed the spec and accepted the WinUI workspace as verified, and confirmed the walkthrough was clear/manageable. Final build SHA256: `54EB0E3B14251D1B920C9D055E10D0285A180D26AA89D9CE16EEF2FDE63E46C4`; launch order was corrected after initial failures. Subsequent user-run 30.37-second minimized-call sample `20261013-145411-MinimizedMeet-08d85b-summary.json` used that executable with no warnings: Callayers 10.197% whole-machine CPU / 306.91 MiB private memory (desktop 7.045% / 121.73 MiB, renderer 3.152% / 185.18 MiB), Brave 4.361% / 3570.65 MiB, combined CPU 14.558%. No new matched raw-camera sample was supplied, so this is not a newly measured overhead figure. Desktop retained memory is higher than the diagnostic foundation sample; the user has accepted workspace usability, and these measurements do not imply an optimization or a universal threshold.

Resource measurements and the focused investigation are complete. On October 13, 2026, the user confirmed the current experience feels acceptable and accepted its resource cost for the MVP. Matched rechecks measured about 9.54 extra whole-machine CPU percentage points with the original 1080p webcam and 5.27 with the smaller 720p webcam. Diagnostic runs confirmed the capture dimensions and approximately 30 camera frames per second. No performance optimization was implemented. Minimized idle measured 0% CPU and 154 MiB retained memory; quit left no tracked Callayers/renderer processes. These samples and this acceptance apply to the observed setup, not a universal performance limit. Revisit performance if later changes or a concrete usability problem justify it; CPU compositor optimization and GPU overlay/composition experiments are deferred from the current MVP work.

### Delivery order and status

1. **Use a proper WinUI workspace — accepted.** The dark header, sample-card sidebar, large preview and camera help are implemented. Saved/imported cards and authentication/AI stay in their following milestones. Whole-card proportional scaling is also implemented and accepted by the user on October 13, 2026 (build SHA256 `4214D343D03FA5C191FD69F14601693EF2F54052DAE93E2AF95C1CAE6CEF6B5E`). Its tested-build record is preserved in Git; no additional resource sample was required.
2. **Return to the same webcam — accepted.** Remember device identity after a working picture; reopen it automatically and offer Retry/selection when unavailable, without silently substituting another camera. User explicitly accepted verification on October 13, 2026; tested build SHA256 `C699F4E235B70289D35DECCE52E1C3EEB8A85757E7741D79A169AC86594DB447`. Accepted outcome/build evidence is preserved in Git.
3. **Annotate the presentation — accepted.** Shapes, text, color/size, selection/movement/resizing, whole-object Eraser and confirmed Clear are complete. Annotations save automatically in the isolated v2 workspace; editing controls stay private. The user explicitly accepted verification, including the text-formatting, editing-outline and newline-rendering fixes, on October 13, 2026.
4. **Sign in and retain the session — accepted.** Fresh Google/Firebase desktop sign-in and account validation run against Cloud Run service `callayers-v2-api` and named Firestore database `callayers-v2`, reusing the verified installed-app OAuth configuration. On October 13, 2026, the user explicitly verified all three runtime checks: Google sign-in/return and webcam picture, remembered session/annotations after reopening, and sign-out stopping live Meet output with neutral unavailable video while preserving annotations on signing back in. Tested workspace executable SHA256: `6E30628CEFE469663EF265D95668982EFCD149EF0B05A5E9612928E41124759C`; backend revision: `callayers-v2-api-00001-z56`. Public health/configuration and rejection of missing/invalid credentials were checked separately. The user also confirmed Google Auth Platform Audience is External and publishing is In production. Branding/verification status and a clean-profile/external-user check remain public-release items; they do not reopen this accepted desktop milestone.
5. **Create an AI card.** Connect generation, local saving, allowance and actionable failure handling. Video must stay responsive during requests. Use the restrictions and website-input behavior in [AI.md](AI.md).
6. **Import and keep cards.** Add image import and the saved local library: search, independent selection/visibility, rename, stacking and delete. Autosave cards, placement and visibility; verify that reopening restores the user's work. Split this outcome further if its walkthrough becomes unwieldy.
7. **Revise and restore a card.** Save one previous version after a successful revision; restore it without losing current placement/visibility. Failed or cancelled requests preserve existing work.
8. **Telemetry — deferred.** Follow the later telemetry requirements in [DATA.md](DATA.md#telemetry-implement-later) and [BACKEND.md](BACKEND.md#telemetry-implement-later); this is not a prerequisite for the current MVP.
9. **Deliver the runnable MVP.** Google Auth Platform Audience is External and publishing is In production, as confirmed by the user on October 13, 2026. Confirm the remaining published/verified Callayers branding (support contact, website/privacy links and domain ownership where required), retain basic sign-in scopes, and test an external Google account on a clean Windows profile. Branding/verification status has not yet been confirmed. Package the v2 backend address with the desktop. Handle camera registration and prerequisites so users can launch without developer commands. Perform a short integrated demo of sign-in, creation/editing, Meet output and saved-work restoration, reusing accepted evidence where relevant.

Trial/payment flows, telemetry, cloud card sync and other explicitly deferred features remain outside this MVP. Do not add them to the walkthrough.

### How we build and verify

Follow [AGENTS.md](../AGENTS.md#feature-verification) for the feature-verification procedure.

The foundation, resource investigation, WinUI workspace, remembered webcam, annotation workflow and integrated Google sign-in are closed. Accepted outcomes and the tested sign-in build/backend are recorded above. AI card creation is next.

The assistant may stop this workspace's running development app to rebuild after giving notice. Follow the remaining delivery order; decide and state any change in priority before treating it as the new plan.

## Reference files

- [Screenshot](references/ui.png): layout reference; remove deck/Save controls and technical status.
- [Annotation bar](references/annotation-bar.png): compact tools and color reference; replace Undo/Redo with Eraser, start with simple swatches.
- `assets/`: PNG, ICO, and SVG logo files.
- [Old WinUI shell](references/winui_shell.h): styling reference only, not code to build or a feature list.

Old source commit: `384e9078d3864d16332178c5cb72776b998ee46d`; pre-cloud baseline: `634e4c3`. Keep the original repository history separately. This folder contains specs and references, not an implementation or full backup.
