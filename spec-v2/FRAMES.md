# Video: MVP and architecture

Callayers combines a webcam, cards, and annotations into one picture. Preview and the calling app receive that same picture. This document defines the video system; [APP.md](APP.md) defines the rest of the product.

## What must work

- Show live webcam video with the selected cards and annotations. Changes appear in preview and camera output; menus and editing handles never enter the outgoing picture.
- Keep video current and editing responsive while generating cards, saving, or handling network failures. Slow processing may skip frames, but must not build up increasing delay.
- Keep an active call running when minimized. Closing quits; there is no close-to-tray mode or background workspace after quitting.
- Stop capture and invalidate live images on quit, sign-out, or confirmed invalid credentials. A temporary backend outage does not interrupt an already running call.
- On camera loss, producer failure, or app crash, supply a neutral unavailable picture. Never keep serving the user's last webcam image indefinitely. Preserve saved work and offer recovery inside the app.

## Simplifying assumptions

| Decision | What it simplifies |
| --- | --- |
| One active webcam and one composition | One source and workspace to coordinate; no scenes, transitions, or multiple-camera mixing. |
| One internal canvas: 1280 x 720 at 30 fps | One layout and timing baseline. Preview scales to the window; output settings are not exposed to users. |
| One finished picture shared by preview and output | No second composition pipeline whose content or placement can drift. Consumers may display different moments, but use the same source. |
| One calling app consuming video at a time | No MVP promise of simultaneous consumers with different requirements. The first verification target is Google Meet in Brave. |
| One overlay page, with one iframe per visible card | The browser arranges cards together; native code receives one overlay instead of capturing each card separately. |
| Self-contained, interactive HTML cards | Local or embedded assets; mouse/keyboard interaction, animation and timers. No external requests, embedded websites, video or audio. |
| Latest available frames, bounded storage | Slow consumers cannot create a queue of old pictures. Drop superseded frames instead of increasing latency or memory. |
| AI and saving operate independently of video | Video uses the last usable content while other work completes. A failed revision leaves the current card intact. |

An iframe contains a card's document and styles. Self-contained describes its dependencies; limited animation describes its workload. These are separate rules. An iframe alone does not make a card inexpensive or protect the whole renderer from a slow script.

## How we build it

```text
Local cards -> WebView2 overlay page
              - sandboxed iframe for each visible card
              - parent controls placement and stacking
              - parent draws annotations
                         |
                  transparent overlay
                         |
Webcam frames ------> compositor -> latest complete picture -> app preview
                                                           -> camera bridge
                                                              -> DirectShow source
                                                                 -> calling app
```

**Capture and composition.** The desktop opens the chosen webcam. A Direct3D 11 GPU compositor fits it to the fixed canvas and combines it with the latest complete overlay. Capture the live WebView2 composition visual as GPU frames using Windows Graphics Capture; blend webcam, cards and annotations on the GPU. Read back only the finished output for the existing CPU shared-memory/DirectShow bridge; use the composed GPU texture for preview. Webcam upload may still be needed; this is not a zero-copy promise. Preserve aspect ratio, using bars where necessary. Placement and annotations use canvas coordinates so resizing the app or changing Windows display scale does not move outgoing content. Preview-only editing controls sit above the finished picture.

**Card rendering.** Use one WebView2 overlay page with a transparent background. Each visible card has a sandboxed iframe; the parent owns its bounds and stacking order. Enforce [AI.md](AI.md)'s restrictions: no external network, navigation, downloads, device access, persistent storage, or direct native-host access. Any allowed card messages use a small validated interface.

**Live rendering and input.** Keep HTML/CSS/JS live and send its rendered frames to the compositor without repeated PNG/JPEG encoding and decoding in the normal video path. The current PNG-capture implementation is a prototype to replace, not the required architecture. Route input inside card bounds to its HTML, accounting for preview scaling and placement. Private border/corner controls take priority for selection, movement and scaling; active annotation tools take priority over card input. Hit overlapping cards in front-to-back order and retain the input target through a drag. No mode switch is required. Allow only the validated state messages described in [DATA.md](DATA.md).

Hidden cards stop rendering and running scripts; merely making an iframe invisible is not sufficient. Save meaningful state before unloading and restore it before showing again or reopening. Timers restore paused; animations restart. Do not restore the JavaScript runtime.

Minimizing during a call keeps cards and timers running. Without a consumer, save state and suspend overlay work; resuming reloads visible cards with saved values, paused timers and restarted animations.

**Frame delivery.** Publish complete frames through a bounded cross-process bridge. The DirectShow source, running on the consumer side, reads the latest completed frame without waiting for the editor, browser, network, or disk. Synchronize access so a reader never sees a buffer being overwritten. Include frame freshness and session identity so an earlier session's image cannot reappear after stopping or restarting.

**Unavailable output.** The camera source produces its own neutral picture when the desktop is absent, explicitly stopped, or no longer publishing fresh frames. It cannot rely on the desktop to draw this after a crash. Brief frame repetition during normal scheduling is acceptable; use a freshness timeout, initially one second, to bound stale output. Explicit stop invalidates output immediately. This controls what Callayers supplies; the receiving app may retain its own cached image.

**Independent work.** Network requests, file writes, and expensive image processing stay off the video delivery path and do not block the browser's UI thread. Publish a successful card revision only when its replacement is ready. Discard late work from a stopped session.

This is a fresh implementation. Git history provides design references and evidence that transparent iframe cards, minimized animation, and virtual-camera delivery worked previously; do not restore or port the old implementation. The previous output used Media Foundation, so it does not establish DirectShow compatibility or a performance advantage. Verify the new output in one chosen calling app. The GPU handoff/compositor is required by the revised MVP; further optimization should follow measured problems.

## Consumer identity (implement with telemetry)

When implementing telemetry, extend the consumer-side DirectShow source and local camera bridge to report the host application for the active consumer. Resolve identity when consumption starts, outside the per-frame delivery work, and expose a normalized app label to the desktop for `video_state_changed`; see [DATA.md](DATA.md#telemetry-implement-later). Verify identity in the target calling apps because helper or broker processes may obscure the application. Report `unknown` when attribution is unreliable and clear identity to `none` when consumption stops or its heartbeat expires. Identity failure must not interrupt video.

Keep the existing one-consumer MVP assumption. Identify browsers as browsers, without trying to identify the calling website. This work is deferred with telemetry: no consumer-identity implementation, bridge changes, or new network heartbeat is required before then. The existing local consumer heartbeat continues to support video lifecycle behavior.

## Open, minimized, and closed

A consumer means an app actively requesting Callayers video, not merely listing available cameras.

| State | Required behavior |
| --- | --- |
| Signed in, workspace visible | Capture and render for preview, whether or not there is a consumer. |
| Minimized, consumer active | Keep capture, card rendering, and output running. |
| Minimized, no consumer | Release webcam, save card state and suspend overlay work. Restore capture/cards when the window or a consumer returns, provided the session is authorized. |
| Close or Quit | If video is in use, ask whether to quit and stop output. On confirmation, invalidate live output, release capture, and exit. No system-tray mode. |
| Signed out or credentials confirmed invalid | Invalidate live output, release capture, and lock the workspace. Keep saved cards. |
| Camera lost, app crashed, or producer stalled | Supply unavailable output. Do not silently switch webcams. Recovery must publish fresh frames before live output resumes. |

The app must be open to supply live video, but its window need not be visible. The consumer-side source may supply unavailable frames after the app quits; the desktop and card renderer do not remain running for that purpose.

## Build order

Delivery order, status and the next step live only in the [consolidated plan](README.md#consolidated-plan--session-handoff). This document defines the target video architecture; the current prototype does not yet meet the GPU/input requirements.

## Minimum verification

The first verification setup is the user's current Windows PC, USB webcam, and Google Meet in Brave. Record the PC identifier, exact webcam name, and Brave version when verification begins. Test a small representative scene with an animated card, a timer card, and an annotation.

The user performs verification. ChatGPT prepares the build, walks the user through each check and its expected result, records the user's reported outcome, and fixes failures. ChatGPT does not independently perform verification or mark checks as passed.

| Check | Pass condition |
| --- | --- |
| Make a short call and change the composition | Receiving picture matches preview content, animation stays live, and editing controls stay private. Video remains usable without growing delay. |
| Interact with a resized card, hide/show it, then reopen | Clicks and typing reach the correct controls; saved values return, timers return paused, and animations restart. |
| Minimize, disconnect the consumer, then reconnect | The call continues minimized; unused capture stops; reconnecting resumes fresh video. |
| Quit normally, then separately force-stop the app during output | Live output stops and the source supplies the unavailable picture instead of the last webcam image. Reopening restores fresh output. |
| Measure preview, minimized output, idle, and quit | Record CPU and app/browser private memory; compare with the baseline below. Investigate unexplained increases or visible slowdown. |

These checks are the initial MVP gate. Add focused tests when a change or failure warrants them; a broad device matrix, forced-stall suite, and one-hour soak are not prerequisites for this first milestone.

## Existing performance baseline

User-provided measurements from the previous implementation; not new measurements of v2. Retain the original machine, build, workload, and CPU accounting details if available. Where those details are missing, use the numbers as directional comparisons rather than exact pass/fail thresholds.

| Previous scenario | Reported result |
| --- | --- |
| Preview without cards | 11.42% CPU |
| Animated card | 11.25% CPU |
| Tray, with or without cards | 0% reported CPU |
| Camera consumer, editor hidden | 5.4% CPU |
| Tray | Roughly 304–394 MB private memory retained |
| Fully quit | No tracked processes remained |

These results do not identify tray CPU or card animation as the main bottleneck. Start any needed profiling with the webcam/preview path. Close-to-quit simplifies behavior and releases retained resources. Compare the old tray-idle case with the new minimized/no-consumer case, and compare full quit separately. Record the new build and measurement conditions beside its results; profile individual stages only if these checks reveal a problem.

References: [WebView2 frames](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/frames), [threading model](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model), [suspension](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2_3), and [DirectShow source filters](https://learn.microsoft.com/en-us/windows/win32/directshow/how-to-write-a-source-filter-for-directshow).
