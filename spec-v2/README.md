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

The user completed the video-foundation walkthrough: webcam/cards/annotations reach Meet, editing handles stay private, minimized calls continue, idle capture stops, reconnection and camera Retry work, and normal quit supplies unavailable output and releases the tracked processes. Preserve the working video engine. On October 13, 2026, the user reviewed the spec and accepted the WinUI workspace as verified. Final build SHA256: `54EB0E3B14251D1B920C9D055E10D0285A180D26AA89D9CE16EEF2FDE63E46C4`; launch order was corrected after initial failures. No additional workspace resource sample was supplied; acceptance rests on the user's reported experience and existing foundation measurements.

Resource measurements and the focused investigation are complete. On October 13, 2026, the user confirmed the current experience feels acceptable and accepted its resource cost for the MVP. Matched rechecks measured about 9.54 extra whole-machine CPU percentage points with the original 1080p webcam and 5.27 with the smaller 720p webcam. Diagnostic runs confirmed the capture dimensions and approximately 30 camera frames per second. No performance optimization was implemented. Minimized idle measured 0% CPU and 154 MiB retained memory; quit left no tracked Callayers/renderer processes. These samples and this acceptance apply to the observed setup, not a universal performance limit. Revisit performance if later changes or a concrete usability problem justify it; CPU compositor optimization and GPU overlay/composition experiments are deferred from the current MVP work.

### Remaining delivery order

1. **Use a proper WinUI workspace — accepted.** The dark header, sample-card sidebar, large preview, compact Select/Pen/Clear toolbar and camera help are implemented. Saved/imported cards, authentication/AI and the remaining annotation tools stay in their following milestones. Follow-up design question: card resizing currently changes the iframe bounds while fixed-size HTML content retains its size; agree content scaling/reflow behavior before treating that as a separate improvement.
2. **Return to the same webcam — next.** Remember device identity; reopen it automatically and offer Retry/selection when unavailable, without silently substituting another camera.
3. **Import and keep cards.** Add image import and the saved local library: search, independent selection/visibility, rename, stacking and delete. Autosave cards, placement and visibility; verify that reopening restores the user's work. Split this outcome further if its walkthrough becomes unwieldy.
4. **Annotate the presentation.** Complete shapes, text, color/size, selection/movement/resizing, whole-object Eraser and confirmed Clear. Save annotations with the workspace and keep editing controls private.
5. **Sign in and retain the session.** Connect Google/Firebase sign-in and account validation through the backend. The finished app requires authentication for editing and live output. Verify return-to-app, remembered session and sign-out stopping output while preserving local work.
6. **Create an AI card.** Connect generation, local saving, allowance and actionable failure handling. Video must stay responsive during requests. Use the restrictions and website-input behavior in [AI.md](AI.md).
7. **Revise and restore a card.** Save one previous version after a successful revision; restore it without losing current placement/visibility. Failed or cancelled requests preserve existing work.
8. **Deliver the runnable MVP.** Handle camera registration and prerequisites so users can launch without developer commands. Perform a short integrated demo of sign-in, creation/editing, Meet output and saved-work restoration, reusing accepted evidence where relevant.

Trial/payment flows, telemetry, cloud card sync and other explicitly deferred features remain outside this MVP. Do not add them to the walkthrough.

### How we build and verify

Follow [AGENTS.md](../AGENTS.md): explain the few high-level outcomes first, then guide one concrete user action at a time, with expected results and exact build commands. Keep scope, progress and the finish visible in a short nested checklist. The assistant implements, compiles, reads user-generated results and fixes failures; the user performs runtime verification. Measure resources when relevant and distinguish recorded measurements from acceptance.

Use one temporary verification file per meaningful improvement. While work is active, it holds the checklist and results. When closed, commit the final outcome and tested build, carry unresolved issues into focused follow-up work, then remove the file from the working tree in a subsequent commit. Git preserves historical evidence; completed checks do not become enduring product requirements.

The foundation and resource investigation are closed. Their tested builds, measurements and acceptance are preserved in Git; give the WinUI workspace its own focused verification checklist.

The assistant may stop this workspace's running development app to rebuild after giving notice. Start with the WinUI workspace; decide and state any change in priority before treating it as the new plan.

## Reference files

- [Screenshot](references/ui.png): layout reference; remove deck/Save controls and technical status.
- [Annotation bar](references/annotation-bar.png): compact tools and color reference; replace Undo/Redo with Eraser, start with simple swatches.
- `assets/`: PNG, ICO, and SVG logo files.
- [Old WinUI shell](references/winui_shell.h): styling reference only, not code to build or a feature list.

Old source commit: `384e9078d3864d16332178c5cb72776b998ee46d`; pre-cloud baseline: `634e4c3`. Keep the original repository history separately. This folder contains specs and references, not an implementation or full backup.
