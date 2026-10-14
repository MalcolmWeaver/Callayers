# Interactive AI card implementation

This breaks down **step 6** of the [master MVP plan](README.md#complete-mvp-sequence): rendering, interaction, state, generation and website tools. Overall priority/status stays in the master plan. [APP.md](APP.md), [AI.md](AI.md), [DATA.md](DATA.md) and [FRAMES.md](FRAMES.md) define the required behavior. Revision follows separately in master step 8.

## Architecture

**Video candidate:** live Ultralight CPU HTML renderer → transparent pixels → CPU composition with webcam/cards/annotations → native preview + existing shared-memory bridge → DirectShow camera → calling app.

Prioritize memory while preserving usable video and interaction. GPU rendering is optional and should be selected only if it lowers RAM in matched measurements. Bounded CPU copies and blending are allowed; use direct pixels rather than repeated PNG/JPEG encoding/decoding. Preserve the old CPU workspace for comparison. Keep application-owned placement, stacking and annotations; verify card isolation and network/file/native-access restrictions before accepting generated content in a different engine.

**Current position:** build 1b accepted by the user on October 13, 2026, including picture quality, private text editing in Meet, minimized calls and idle/resume. Tested Ultralight SHA256: `2AB6194E08961397502FCD32E692C6D62FB70EF853E1B15AFCC18647062A5C5E`. Its output remains preserved in `build/ultralight-workspace-x64`. Build 2, fixed interactive input/state, was accepted by the user on October 13, 2026 in `build/interactive-workspace-x64`. Tested SHA256: `21DA5307AFC1FD683AB4E75EF54C9C35CC615C11FA1E445CFFB1C0BAC501F526`. Next is build 3, AI card creation. The existing WebView2 text editor is retained; its memory cost remains a later measurement.

**Full-app resource evidence, October 13, 2026:** the user supplied clean 30-second-class samples with no warnings. Minimized Meet: Ultralight 7.644% CPU / 157.22 MiB private memory versus previous CPU workspace 8.308% / 283.90 MiB (44.6% less private memory). Restored preview with cards: Ultralight 6.785% / 151.94 MiB versus previous workspace 9.849% / 284.81 MiB (46.7% less private memory). Reports respectively: `20261013-201219-MinimizedMeet-d67ab7-summary.json`, `20261013-201311-MinimizedMeet-f6b1fa-summary.json`, `20261013-201549-PreviewCards-4a5b49-summary.json`, `20261013-201453-PreviewCards-722eb2-summary.json`. Tested Ultralight SHA256: `2AB6194E08961397502FCD32E692C6D62FB70EF853E1B15AFCC18647062A5C5E`; previous workspace: `8438DA2FA579E3B42C7C07D0EC9C00A1D849F33CDD97C97A481FD79461B15835`. Both Ultralight samples had no attributed browser processes; they do not establish the memory cost during/after text editing. Picture quality, private editing and idle/resume were subsequently accepted explicitly by the user. Resource comparison is complete for these scenarios; no new build was made for this report.

**Input:** preview coordinates → private handles first → frontmost card bounds → scaled HTML coordinates. Border controls select/move; corners scale; clicks, scrolling and typing inside operate the HTML. Keep the target through a drag, show keyboard focus and release it on outside clicks. Active annotation tools take priority. No Interact/Arrange switch. HTML controls are visible in outgoing video; private handles are not. Call viewers see the results but cannot interact remotely.

**State:** card → validated app-owned state messages → local card/workspace save. Save meaningful values, not the DOM/JavaScript runtime. Restore values before showing; unloaded timers restore paused and animations restart. Cards have no internet, browser storage or arbitrary native-host access.

**AI:** prompt → authenticated `/generate` → model + backend website reader → validated name/HTML/assets → desktop local save → live interactive card. The backend owns credentials, allowance and request status; the desktop owns cards, placement and state. Network, saving and generation stay off video/UI work.

## Small build sequence

**Build 3 creation check accepted, October 13, 2026:** the user deployed generation and confirmed the guided creation check: create Verification Counter, see an initially unchecked library row and remaining allowance, show it via checkbox, and use +1/Reset. Tested desktop: `build/generation-workspace-x64/Release/Callayers.exe`, SHA256 `3B052EDFE3D4749A466362C72827CAA5D0B8348D7063F010C8EE80F283B4C0EA`. Public deployed health reports `generationReady:true`. Backend reservation/ownership/duplicate handling and provider integration pass 15 synthetic tests; desktop/state/renderer checks pass 7. User confirmed 10 generations per account per UTC day, set independently on Cloud Run as `USER_DAILY_GENERATIONS`; the script leaves that value alone and the backend validates it at startup. Configured model is `gpt-4.1-mini`. Accepted build 2 is preserved. Generated-card hide/reopen/restoration and cancellation checks remain for the guided walkthrough. The user requested discussion of improved opacity styling and general separation of runtime environment settings from deployment; these changes are not implemented. Build 4 has not started.

**Build 2 accepted:** `build/interactive-workspace-x64` provides the fixed score/name/timer fixture, scaled HTML mouse/keyboard/scroll input, border/four-corner manipulation, focus release and validated autosaved state. Hiding/reopening restores a paused timer. The user confirmed all four runtime checks passed: direct controls/keyboard and Meet updates, movement/scaling/overlap and drag release, hidden/reopened paused-state restoration, and annotation-tool priority. Build 3 has not started. The trusted named-frame snapshot/restore interface is a proof of the state contract, not acceptance of generated-content isolation.

Start with fixed content, not AI. Each row is a small build; prepare focused runtime checks in chat and wait for the user's results before advancing. Add local automated tests where they protect input/state, allowance and website handling. No backend deployment is needed to prove the renderer.

| Build | What it establishes | Backend deploy? |
| --- | --- | --- |
| 1a. CPU renderer comparison | Same static, animated and timer cards in isolated Ultralight and WebView2 CPU tests; compare appearance, animation and memory. | No |
| 1b. CPU video integration | Direct Ultralight pixels blended with webcam/annotations on CPU; native preview and existing camera bridge. Verify private editing, minimized calls and idle shutdown, then compare full-app memory against the preserved CPU workspace. | No |
| 2. Fixed interactive card | Buttons, text input and a timer; bounds/handle hit testing, scaling, keyboard focus, overlap and drag targeting. Save/restore values, with unloaded timers paused. This establishes the contract AI cards must use. | No |
| 3. AI creation | Configure provider secret/model/limit; authenticated generation, atomic allowance and duplicate-request handling. Prompt/Create/Cancel, no shape selector. Save/list the result initially hidden, show via checkbox, and restore after reopening. | Yes: generation API/configuration |
| 4. Website tools | Model calls the backend reader for one supplied public page. Bare/path/HTTPS URLs, real summary text, requested styling/logo/images, bounded safe downloads and embedded offline assets. Explain missing sources/branding; no crawling, search or login. | Yes: reader and model/tool loop |
| 5. Failure/integration pass | Responsive video during requests; useful allowance/reset, cancellation and save/source errors. Discard late results. Lost response requires explicit new generation; repeating an ID never generates/counts twice. | Only if server fixes are needed |

Failures are handled as each feature is built; build 5 checks their combined behavior. Build 3 deploys after local backend tests pass; build 4 does the same for tool use. Extend the existing deployment script for provider settings/secrets and bounded generation/tool timeouts. It currently deploys sign-in only. Generation changes must preserve accepted authentication.

## Performance risk and decision points

The main early risks are renderer compatibility/isolation, minimized rendering and total memory after webcam, UI and private editing are included. Memory is the primary optimization goal; CPU and visible responsiveness are usability guardrails. Compare matched workloads; no arbitrary pass threshold is set. Renderer-only results do not predict full-app savings.

If the proof fails or integration regresses usability, diagnose that specific stage before adding AI. Do not spend indefinitely optimizing or quietly revert to PNG: bring the measured blocker and a concrete alternative back for a scope decision. Keep the accepted executable available during these builds. Later, generated JavaScript can also be expensive; start with representative fixed cards and bound generation/tool work so backend failures do not stall video.

Model/tool exchanges share one generation/allowance unit. Website content is untrusted reference material, never tool instructions. Reading is mandatory when the request depends on a page; a URL alone is not evidence. Keep prompts/results out of backend storage/logs. No document tools, image-generation tools or live-data integrations in this MVP.

## Revision in step 8

Reuse generation/tools for the selected card only. Preserve unaffected content and images; validate and safely save before replacing. Retain one previous content/name/assets version. Restore locally without AI/allowance, consuming the backup and retaining placement/visibility. Replacement content starts with default state. Failed, cancelled or outdated requests change neither version.
