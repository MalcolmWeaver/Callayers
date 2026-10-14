# Callayers v2

Sign in, choose a webcam, create interactive cards, annotate, and show the result in a call.

## Read the specs

- [APP.md](APP.md): user experience.
- [AI.md](AI.md): generation and website tools.
- [DATA.md](DATA.md): local saving, state and allowances.
- [BACKEND.md](BACKEND.md): API and deployment.
- [FRAMES.md](FRAMES.md): rendering, input and video delivery.

These define the target product. This is a fresh implementation; v1 is a design reference, not code to restore or port.

## Consolidated plan — session handoff

**This is the master MVP plan.** Overall order, status and the next step live here. [AI implementation](AI-IMPLEMENTATION.md) breaks down the AI milestone without changing this order. Other specs define behavior; implementation notes describe existing code.

### Current position

Updated October 13, 2026. The video foundation, resource investigation, WinUI workspace, remembered webcam, annotations (including text-editing fixes), proportional card scaling and Google sign-in are accepted for their tested builds. AI generation, interactive cards and GPU composition are not implemented. Earlier acceptance does not verify the expanded scope.

**Next: step 6, interactive AI cards.** User-run full-app samples show 44.6–46.7% less private memory for the isolated Ultralight CPU workspace, with lower CPU, as recorded in [AI implementation](AI-IMPLEMENTATION.md). Build 1b is accepted, including picture quality, private editing and idle/resume. Build 2 is accepted for its tested executable, including input, positioning, saved values and annotation priority. Next is build 3: AI card creation. Text-editor memory remains unmeasured. Memory is the priority; GPU rendering is optional and should be selected only if it lowers RAM. Full local interaction and explicit backend website-tool use remain MVP requirements. No completion date is set.

### Complete MVP sequence

| Step | Outcome | Status |
| --- | --- | --- |
| 1. Video foundation and resource baseline | Webcam, cards and annotations reach Meet; minimized calls, idle shutdown, recovery and unavailable output work. Preserve the recorded baseline. | Accepted |
| 2. WinUI workspace | Header, library sidebar, preview, camera help and proportional card scaling. | Accepted |
| 3. Remembered webcam | Reopen the same working device; offer Retry/selection without silently substituting another. | Accepted |
| 4. Annotations | Pen/shapes/text, selection, resizing, eraser, clear and autosave; editing stays private. | Accepted |
| 5. Google sign-in | Browser return, remembered session, account validation and sign-out stopping output while retaining local work. | Accepted |
| 6. Interactive AI cards | Memory-focused HTML rendering/composition, direct HTML interaction, saved state, AI creation, allowances and website tools. Ultralight CPU integration accepted; fixed interactive input/state is accepted. AI creation is next. Architecture and small build steps live in [AI implementation](AI-IMPLEMENTATION.md). | In progress |
| 7. Image import and saved library | Import images; search, rename, visibility, stacking and confirmed delete. Restore cards, placement and visibility. Basic card/state saving arrives in step 6. | Not started |
| 8. AI revision and previous version | Revise a selected card, preserve referenced images and keep one previous version. Restore content/name with current placement/visibility and default replacement state. Failed/late responses preserve newer work. See [AI implementation](AI-IMPLEMENTATION.md#revision-in-step-8). | Not started |
| 9. Telemetry and consumer identity | Requirements remain in DATA.md/BACKEND.md/FRAMES.md. Retained in the roadmap, explicitly deferred beyond the runnable MVP. | Deferred |
| 10. Runnable MVP release | Installer handles registration/prerequisites/backend address. Confirm Google branding and external-account sign-in on a clean Windows profile. Integrated creation/interaction/Meet/restoration demo. | Not started |

Deliver each step through small improvements with short verification in chat. Website reading is required for step 6 to finish. Lower resource cost is a measured goal, not an assumed GPU benefit. Preserve accepted behavior throughout.

Billing/trials, cloud sync, remote participant interaction and other explicitly excluded features remain outside this MVP.

### How we build and verify

Follow [AGENTS.md](../AGENTS.md#feature-verification). Prepare builds and put short verification steps in chat; the user reports runtime results. Record accepted outcomes and tested builds in Git. Do not create verification documents. The assistant may stop this workspace's development app to rebuild after giving notice.

### Accepted evidence

Acceptance below was recorded October 13, 2026; details remain in Git history and conversation. SHA256 identifies the tested executable, not the current target specification.

| Accepted outcome | Tested executable SHA256 / backend |
| --- | --- |
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
