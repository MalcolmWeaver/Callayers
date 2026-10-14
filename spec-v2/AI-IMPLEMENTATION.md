# Interactive AI card implementation

This breaks down **step 6** of the [master MVP plan](README.md#complete-mvp-sequence): rendering, interaction, state, generation and website tools. Overall priority/status stays in the master plan. [APP.md](APP.md), [AI.md](AI.md), [DATA.md](DATA.md) and [FRAMES.md](FRAMES.md) define the required behavior. Revision follows separately in master step 8.

## Architecture

**Video candidate:** live Ultralight CPU HTML renderer → transparent pixels → CPU composition with webcam/cards/annotations → native preview + existing shared-memory bridge → DirectShow camera → calling app.

Prioritize memory while preserving usable video and interaction. GPU rendering is optional and should be selected only if it lowers RAM in matched measurements. Bounded CPU copies and blending are allowed; use direct pixels rather than repeated PNG/JPEG encoding/decoding. Preserve the old CPU workspace for comparison. Keep application-owned placement, stacking and annotations; verify card isolation and network/file/native-access restrictions before accepting generated content in a different engine.

**Current build:** isolated Ultralight CPU workspace integration in `build/ultralight-workspace-x64`, pending user verification. The renderer-only comparison reported 50.02 MiB / 1.490% CPU for Ultralight versus 206.45 MiB / 4.866% for WebView2; these do not establish full-app savings. Next verify the same fixed cards, webcam, private text editing and minimized Meet, then compare against the preserved CPU workspace. The existing WebView2 text editor is retained, so measure both before and after editing. Direct card input/state remain later work.

**Input:** preview coordinates → private handles first → frontmost card bounds → scaled HTML coordinates. Border controls select/move; corners scale; clicks, scrolling and typing inside operate the HTML. Keep the target through a drag, show keyboard focus and release it on outside clicks. Active annotation tools take priority. No Interact/Arrange switch. HTML controls are visible in outgoing video; private handles are not. Call viewers see the results but cannot interact remotely.

**State:** card → validated app-owned state messages → local card/workspace save. Save meaningful values, not the DOM/JavaScript runtime. Restore values before showing; unloaded timers restore paused and animations restart. Cards have no internet, browser storage or arbitrary native-host access.

**AI:** prompt → authenticated `/generate` → model + backend website reader → validated name/HTML/assets → desktop local save → live interactive card. The backend owns credentials, allowance and request status; the desktop owns cards, placement and state. Network, saving and generation stay off video/UI work.

## Small build sequence

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
