# macOS app

Callayers on Mac follows [APP.md](APP.md), using the existing backend and local card model. Apple Silicon is the primary product target. Intel is a compatibility target; the user's 2018 Mac mini is the initial Intel verification machine. Its installed macOS version is not yet known.

## Product and interface

Use the Windows app as the reference for workspace layout and interactions, including card selection, visibility, arrangement, annotation tools, and private editing. Adapt menus, keyboard shortcuts, permissions, and window behavior to macOS conventions. Reusing Linux code must not require preserving its current interface choices.

Build the Mac interface in SwiftUI, with AppKit views for the live preview and detailed pointer, keyboard, focus, and text-editing behavior. Both processor architectures use the same Mac interface source. Qt Widgets is not the Mac interface framework.

## Architecture

- **Interface:** SwiftUI and AppKit, written in Swift.
- **Rendering and composition:** reuse suitable C++ code through a narrow bridge. Existing Windows and Qt types require adaptation; neither implementation is already an independent portable core.
- **Webcam:** AVFoundation, with remembered device identity, camera permissions, and explicit recovery.
- **Cards:** Ultralight CPU rendering into transparent pixels, composed with webcam frames and annotations. Start with CPU composition; change it only to address a measured concern.
- **Virtual camera:** an app-bundled Core Media I/O camera extension, receiving completed frames from the app and publishing the Callayers camera. Keep private editing controls out of delivered frames.
- **Sign-in and saving:** macOS Keychain for session secrets and standard Mac user directories for cards and preferences. Reuse the existing backend protocols.
- **Distribution:** separate native Apple Silicon and Intel downloads, with the application and extension signed and notarized for distribution.

Use the modern camera extension on both architectures. Do not add a legacy DAL camera plug-in or require OBS for the product. Core Media I/O camera extensions require macOS 12.3 or later; the app's final minimum version also depends on the chosen SwiftUI APIs and renderer SDK.

## Renderer versions

Split renderer versions by architecture. Apple Silicon uses Ultralight 2.0.0-beta.2. Intel uses Ultralight 1.4.0, whose SDK identifies itself as 1.4.0.1b4b800. Both archives are pinned by SHA-256 in `macos/Config/renderer-sdks.json`. Intel compatibility must not hold back the primary Silicon renderer.

Keep these differences behind a renderer interface for loading cards, delivering input, extracting frames, and reading or restoring state. Share card HTML, application behavior, and the rest of the Mac source wherever compatible. Separate downloads allow architecture-specific dependencies without making a universal application bundle an initial requirement.

The Intel integration must adapt version differences rather than merely relink the current implementation. In particular, current code evaluates JavaScript in named card frames; Ultralight 1.4 exposes a different evaluation API. Preserve restricted card execution while adapting that bridge.

Renderer acceptance covers transparent pixels, representative card styling, animation, input, saved interactive state, and isolation. HTML and JavaScript support alone do not establish compatibility with existing Callayers cards. If the Intel adapter encounters a blocker, report the specific failed behavior before changing the primary Silicon architecture.

## First implementation checkpoint

Prove the renderer integration before building out the Mac workspace: acquire and pin both SDKs, prepare small native rendering checks, and exercise representative cards and state through the version adapters. Start with the Intel compatibility uncertainty while keeping Silicon as the primary product target.

Linux can support source preparation and shared-code checks. Mac SDK compilation requires a macOS build environment; actual interface, camera permission, extension activation, and calling-app behavior require Mac runtime verification. Prepare small builds and put verification instructions in chat under [AGENTS.md](../AGENTS.md).

Status: renderer integration and the first persistent workspace are implemented in source. The SwiftUI library supports selection independently of visibility, search, rename, duplicate, confirmed deletion, hide all, and bringing a card to the front. The AppKit preview composites independent card views, with private border movement and corner resizing plus sidebar placement controls. Cards retain their own HTML, interactive values, placement, visibility, and drawing order. The development-only Add counter control supplies cards until creation and import are implemented.

`WorkspaceModels.swift` validates a versioned document; `WorkspaceStore.swift` stores it under `~/Library/Application Support/Callayers/DevelopmentWorkspace/`. Atomic writes retain a previous saved workspace. Unreadable files block editing and saving; explicit backup recovery preserves the original in a separate file. `WorkspaceModel.swift` coordinates periodic snapshots and saving before hide, duplicate, and quit. The close button and Cmd-Q use the same save path, and a failed save permits canceling quit. The old renderer-preview UserDefaults value is left untouched; this development workspace starts empty.

The shared renderer checks pass against the actual Linux builds of both SDK versions, including independent multi-card input/state, transparency, animation, isolation, rejected file/network access, and state validation. Both Mac SDK archives have been checked for version, checksum, architecture, and bundled resources. Intel Swift/Objective-C++ compilation, workspace storage checks, native renderer checks, and the user card checkpoint now pass on macOS 15.8.1 with Xcode 26.3. Apple Silicon compilation/runtime verification remains pending. The renderer-per-visible-card composition is an initial preview implementation; native performance and the final video pipeline still need evaluation.

The Xcode project contains the development preview, bundled `CallayersCamera` system extension, and `RendererChecks` targets. With full Xcode selected and Python 3 available, `bash scripts/build-macos.sh --open` builds a locally signed preview. The script acquires the host SDK, runs workspace and camera-picture checks, builds the app and extension, verifies signatures, and runs native renderer checks. `--arch arm64` and `--arch x86_64` select explicit builds; execution checks require a matching host. Camera activation requires `bash scripts/build-macos.sh --team TEAM_ID`, an Apple Development identity, Xcode account access, and the resulting app installed in `/Applications`. Signed builds use `build/macos-camera-<arch>`; locally signed previews use `build/macos-<arch>`. `DEVELOPER_DIR` may point to an Xcode installation when it is not the system-selected developer directory. Developer ID distribution and notarization remain later work.

`.github/workflows/macos.yml` prepares separate Intel and Silicon build/check jobs; adding it locally does not run CI. IME composition and full editing shortcuts belong to the later workspace implementation. The user's paid Apple Developer team and development signing identity are configured for the Intel camera checkpoint.

## Implementation plan

Intel Mac checkpoint accepted October 16, 2026: the user verified the native workspace preview and reported "yes, cards work" after receiving the card interaction, arrangement, and reopening checks. The x86_64 Debug build completed with Xcode 26.3 on macOS 15.8.1; workspace checks, native Ultralight 1.4 renderer checks, and local signature verification passed. Tested app executable SHA-256: `8bad7cdc0206a3e5935c6a452a0a99ab05cdc2d3c28d4231dabebe568e9850f9`. Apple Silicon native verification remains pending. Next is checkpoint 2, beginning with generated-picture camera delivery.

This is a substantial platform port: the interface, camera capture, camera extension, and system integration are new Mac implementations. Reuse the web assets, backend protocols, behavior, and suitable algorithms. Do not first refactor Windows and Linux into a universal core. Keep the new source under `macos/`, with one implementation shared between Intel and Silicon except for the renderer adapters and dependency selection.

### 1. Build and renderer proof

Prepare an Xcode project with a native app target and C++ renderer bridge, architecture-specific SDK acquisition, and a macOS build workflow. Compile both architectures in a macOS environment. Test representative transparent cards, animation, mouse and keyboard input, state restoration, and isolation. Resolve the Ultralight 1.4 frame scripting difference here.

This checkpoint establishes whether the chosen renderer split can support the existing product. An Intel adapter failure must be reported as that specific limitation; it does not automatically change the Silicon architecture. Linux source checks cannot substitute for compiling either Mac target.

### 2. Camera delivery proof

Add AVFoundation capture and a minimal camera extension. First deliver a generated test picture, then webcam plus one card, through the selectable Callayers camera. Prove signing, activation, consumer start/stop, frame timing, disconnect recovery, and unavailable output before investing in the full workspace.

Start by evaluating the supported Core Media I/O sink stream for app-to-extension delivery, with a source stream for calling apps. Keep the frame transport behind a small interface. Bound pending frames and drop superseded frames. Keep capture, composition, and card execution in the app; the extension delivers completed frames. Stream activity must drive resource lifecycle independently of whether a consuming application can be named.

Apple signing identities, bundle identifiers, and app-group configuration are needed to validate activation. Prepare the build configuration here; runtime activation and permissions remain Mac checks.

First camera increment prepared October 16, 2026: the signed Intel app embeds `com.callayers.renderer-preview.camera` and provides Camera setup controls for activation, removal, approval, and error reporting. The extension publishes a stable Callayers device with a 1080p/30 BGRA stream. The user confirmed installation; macOS reports the version 0.2.0/build 2 extension activated and enabled. Calling-app delivery was not yet accepted.

The webcam increment is prepared as version 0.3.0/build 3 and installed in `/Applications/Callayers Renderer Preview.app`. It adds physical-webcam selection and permission handling, remembers the selected device, composites webcam and visible cards, and transports completed frames through a bounded Core Media I/O sink to the extension. Selection borders and resize handles remain private to the app preview. Capture and composition respond to preview visibility and camera consumers; stale or disconnected input clears rather than remaining frozen. The extension shows an unavailable picture when fresh app frames are absent. Native checks pass for composition orientation, card alpha and placement, hidden cards, timestamps, stale-frame rejection, buffer-pressure dropping/recovery, workspace persistence, and renderer behavior. The signed Intel build and installed bundle signatures validate. Updating the installed extension, webcam-and-card delivery to a calling app, and quit/reopen recovery await user verification; automated checks do not establish those runtime outcomes.

Webcam preview accepted October 16, 2026: the user reported "webcam and cards work now" with version 0.3.0/build 3. Their Meet screenshot establishes that the earlier generated test picture reaches the calling app, but does not establish webcam-and-card delivery. A subsequent system check confirmed extension 0.2.0/build 2 remains activated while the installed app embeds extension build 3. The next action is to activate the bundled update through Camera setup and verify the composition in Meet. Tested app debug-library SHA-256: `2ff8c685a582c742d962c9ba1a479b657eaff6819d923bec0662c21b8e7501dc`; bundled extension executable SHA-256: `00538047d754241afff5d81ab0be90b197eb880dcc57aa2caa0ac2c3d194bfd1`.

Producer authentication repair prepared October 16, 2026: Core Media I/O reported `unknown` for correctly signed producers, and ordinary PID-based Security lookup failed inside the camera sandbox. The repair uses `kSecGuestAttributeDynamicCode` to validate the kernel-backed process signature, requiring an Apple certificate, the Callayers application identifier, and the extension's developer team. Unknown identities do not bypass validation. On installed build 6, a signed Callayers-identity probe was accepted, a same-team/different-identifier probe was rejected, and an ad-hoc/same-identifier probe was rejected; extension logs confirmed signature rejection. `macos/Tests/CameraIdentityChecks.mm` retains the live-sink regression probe. Build 7 adds first-fresh-frame diagnostics; signed builds and workspace, video, camera-picture, and renderer checks pass. Build 7 is installed, but macOS reported a launchd replacement conflict (`Operation already in progress`) after activation; a clean app restart and repeat activation did not restore the camera launch service. A Mac restart is the next recovery step; live frame delivery remains pending. Installed app debug-library SHA-256: `dfeb66950ee30484fe3e067b830221a34c5194c2d1c97940044c782c23bfe24a`; extension executable SHA-256: `53cadc875fc3c45f208bc04f1819afb731c6e2f234badf49587813f48a0eebd1`.

Frame-queue repair prepared October 16, 2026: after restarting, the actual app passed authentication and started the sink but delivered no frames. A focused signed probe established that `CMIOStreamCopyBufferQueue` with a null callback returned success and a null queue; a non-null callback returned a usable queue. App 0.3.5/build 8 now registers that callback and rejects null queues. The activated extension remains 0.3.4/build 7, preserving Callayers-only authentication. A synthetic transport probe delivered fresh frames to the extension, and after installing build 8 the real app also produced the extension's first-fresh-frame receipt log. The signed build, camera/video/workspace checks, and native renderer checks passed. `CameraTransportChecks.mm` provides an AVFoundation round-trip fixture, but its standalone execution was blocked by missing camera permission for that test process; no full source round-trip pass is claimed. The user subsequently confirmed the live-camera verification was fixed.

Live virtual-camera delivery accepted October 16, 2026: the user reported "fixed" after receiving the Meet webcam-and-cards and card-change verification steps. Tested app: 0.3.5/build 8; activated camera extension: 0.3.4/build 7. App debug-library SHA-256: `43a59209ae102ac94fad6b6aa0e3a1c9a62c396ca1c65c3b178badd538807d71`; extension executable SHA-256: `53cadc875fc3c45f208bc04f1819afb731c6e2f234badf49587813f48a0eebd1`. This accepts live delivery in Meet while preserving Callayers-only producer authentication. Quit/reopen, disconnect recovery, and Apple Silicon runtime acceptance remain separate checks.

Unattended checks now use `python3 scripts/check-macos-unattended.py --seconds 300` after a signed host build, with Callayers and calling apps closed. The runner executes workspace storage, generated-picture, composition, and real renderer checks, then launches a Debug-only mode inside the signed host app. That mode opens only the Callayers virtual camera, sends changing synthetic frames through the production transport, checks stale-input fallback and producer/consumer restart recovery, and exits without creating a workspace window. It reuses existing camera permission and fails without prompting if permission is missing. Each run writes a uniquely identified JSON report and logs under `build/macos-camera-<arch>/unattended/`; an app watchdog and runner deadline bound stalled runs. A process-scoped `caffeinate` assertion prevents idle system sleep during the suite. Actual disconnected-monitor/webcam acceptance remains a user check; this does not establish physical webcam behavior, UI usability, or calling-app acceptance without a display.

Agent-run unattended suite passed October 16, 2026 on the Intel Mac: workspace, camera-picture, composition, native renderer, 120 seconds of changing virtual-camera output, stale-input fallback, producer reconnect, and consumer restart. Saved workspace and backup hashes were unchanged. Tested Debug app dylib SHA-256: `d00da42c98dd6a54b9b39d7d12d743d4c7fc6402f3a557e1a4677ccb728ce16f`; installed extension remained 0.3.4/build 7. Local report: `build/macos-camera-x86_64/unattended/d5dee12f-606b-40e6-b333-2e83f2d2166d/summary.json`. The monitor and webcam were still connected; user acceptance of the disconnected configuration is pending.

Live-webcam unattended mode adds `--webcam` to the runner. It uses the saved camera (or the sole available camera if none is saved), production `CameraCapture` and `FrameComposer`, and an encoded test overlay. The consumer verifies bounded in-memory fingerprints across the overlay and webcam image against freshly captured, composed samples. Webcam frames, fingerprints, and video are not written to disk. The runner still executes the synthetic preflight and existing storage/renderer/composition checks. `--hours 8 --seconds 300 --webcam` repeats bounded webcam cycles for up to eight hours, including capture/producer and consumer restarts. It stops on failure, saves per-cycle results and progress, rejects overlapping suites, and holds an idle-sleep assertion for its lifetime. Interrupting the runner ends its isolated test app through an owner-process watchdog. This tests capture and composition without opening or changing the normal workspace; full native UI and physical unplug/replug acceptance remain separate.

Agent-run live-webcam preparation passed October 16, 2026 on the Intel Mac using USB CAMERA: two 30-second webcam cycles plus fallback, webcam/producer restart, and consumer restart checks; synthetic preflight and all existing native checks passed. A separate cancellation check confirmed exit status 130, an interrupted (not passed) report, termination of the isolated app, removal of its sleep assertion, and rejection of an overlapping runner. Saved workspace SHA-256 remained `1e219b6fb005735c8a849d215090af125b7e2522220a376c401dd7556a26cf24`. Tested Debug dylib SHA-256: `7ced953a51594e2bd89c9c7fcc974f4134ceaf0255bad1ee0d5194e09cefa9bc`. Reports: `build/macos-camera-x86_64/unattended/0bc1a9c8-e323-46e3-a0cf-447ae924c160/summary.json` (passed) and `build/macos-camera-x86_64/unattended/bdf8db0b-d6d5-4815-8f46-84701202a98c/summary.json` (deliberately interrupted). Extended-duration and disconnected-monitor acceptance remain pending; no eight-hour run was started.

### 3. Native workspace and local persistence

Build the SwiftUI sidebar, camera selection, creation/editing controls, and annotation toolbar using the Windows UX as the reference. Embed an AppKit preview for interaction and private text editing. Add remembered cameras, safe local saves, imported images, card state, and previous-version restoration.

Keep the composition presented to calling apps distinct from private selection handles and annotation drafts. Verify text positioning, proportional resizing, keyboard focus, and saved-state behavior together with the preview. These interactions are a larger part of the interface work than the surrounding buttons and menus.

Image-import increment built October 16, 2026: PNG/JPEG/BMP import normalizes orientation and bounds decoded dimensions, stores local PNG assets separately from card HTML, and resolves placeholders only inside the renderer. Cards now retain independent canvas dimensions with proportional placement/input routing. Version 1 workspaces upgrade to version 2 without changing legacy dimensions. Agent checks passed for decoding, JPEG orientation, persistence, missing/corrupt assets, legacy loading, real Ultralight pixels/transparency, custom dimensions, and existing renderer/storage/composition checks. Native file-picker interaction and visual acceptance remain pending; existing user workspace files were not migrated by these fixture checks.

### 4. Account and backend integration

Add browser sign-in and callback handling, Keychain session storage, renewal and sign-out, AI creation/revision, cancellation, allowance handling, and telemetry with `platform: macos`. Use the existing API. The backend already accepts the Mac platform identifier; no Mac-specific backend service is planned.

Reuse current safeguards against late responses overwriting newer work. Separate connection detection from best-effort consumer identity; use an unknown identity when the actual calling app cannot be attributed, without interrupting video.

### 5. Packaged builds and runtime acceptance

Bundle architecture-matched renderer libraries, resources, web assets, license notices, and the camera extension. Produce separately signed and notarized Silicon and Intel downloads. Verify installation, sign-in, real calling-app output, minimized behavior, quit/reopen, and camera recovery on physical Macs. Keep verification instructions in chat, grouped by each small improvement.

Prepare versioned release artifacts and hosting for the automatic-update feed and public downloads in the next two milestones. No cross-architecture performance or compatibility claim follows from an Intel-only runtime check.

### 6. Automatic updates

Check for updates automatically and select the correct Intel or Apple Silicon download. Verify release authenticity before installation, preserve saved work, and apply updates at a safe exit/start when camera output is idle. Handle camera-extension updates, required system approval or restart, and failed-update recovery without interrupting an active call.

Verify an actual upgrade from one installed build to the next on both architectures, including saved-work preservation and camera availability after the update.

### 7. Public Mac launch

Add Mac downloads and installation instructions to the existing Callayers website. Publish the signed and notarized Intel and Apple Silicon builds and their update feed, making each versioned artifact available before publishing the feed that references it.

Verify the complete path from website download through installation, sign-in, and camera use. The release target is a publicly downloadable Mac app with working automatic updates; both milestones are part of this plan.

## Planned files

The names below describe the expected boundaries; small helpers may be combined or separated during implementation. Expect roughly 40–50 source, configuration, workflow, and test files for the full port, introduced by checkpoint rather than as one scaffold.

### Build and packaging

- `macos/Callayers.xcodeproj/project.pbxproj` and `macos/Callayers.xcodeproj/xcshareddata/xcschemes/Callayers.xcscheme`: app, renderer, extension, and test targets.
- `macos/Config/Build.xcconfig`: shared build settings and architecture-specific renderer paths.
- `macos/Config/AppInfo.plist` and `macos/Config/CameraInfo.plist`: application metadata, permissions, and extension metadata.
- `macos/Config/App.entitlements` and `macos/Config/Camera.entitlements`: signing capabilities and communication configuration.
- `scripts/acquire-macos-renderer.py`: versioned, checksummed SDK acquisition for each architecture.
- `scripts/build-macos.sh` and `scripts/package-macos.sh`: reproducible builds and bundled, signed distribution artifacts.
- `.github/workflows/macos.yml`: Mac compilation and suitable automated checks for both architectures. Adding the file does not itself publish or run it remotely.

### Native interface and application state

- `macos/App/CallayersApp.swift` and `macos/App/AppModel.swift`: app entry, coordinated state, lifecycle, and recovery.
- `macos/App/WorkspaceView.swift`, `macos/App/LibraryView.swift`, and `macos/App/CardPromptView.swift`: workspace, card library, and Create/Edit controls.
- `macos/App/SignInView.swift` and `macos/App/CameraPickerView.swift`: sign-in and camera selection states.
- `macos/App/PreviewView.swift`: AppKit preview, pointer routing, selection, and resizing.
- `macos/App/AnnotationToolbar.swift` and `macos/App/AnnotationEditor.swift`: drawing tools, text editing, and private editing state.
- `macos/App/Models.swift`, `macos/App/WorkspaceStore.swift`, and `macos/App/ImageImport.swift`: Mac models, local persistence, image validation, and saved image assets.

### Rendering and video

- `macos/Renderer/Renderer.h` and `macos/Renderer/Renderer.cpp`: C++ scene renderer, resource restrictions, transparent output, input, and card state.
- `macos/Renderer/SDKCompatibility.h`: isolated differences between Ultralight 1.4 and 2.0; add version-specific implementation files if this outgrows a small adapter.
- `macos/Renderer/RendererBridge.h` and `macos/Renderer/RendererBridge.mm`: narrow Swift-facing Objective-C++ interface.
- `macos/Video/CameraCapture.swift` and `macos/Video/FramePipeline.swift`: AVFoundation capture, composition, frame ownership, and scheduling.
- `macos/Video/VirtualCameraClient.h` and `macos/Video/VirtualCameraClient.mm`: app-side Core Media I/O delivery and stream state.

### Camera extension

- `macos/CameraExtension/main.swift`: separate extension process entry.
- `macos/CameraExtension/CameraProvider.swift`, `macos/CameraExtension/CameraDevice.swift`, and `macos/CameraExtension/CameraStream.swift`: provider registration, Callayers device, incoming frames, outgoing streams, and fallback picture behavior.

### Services and checks

- `macos/Services/AuthController.swift`, `macos/Services/AuthCallback.swift`, and `macos/Services/SessionKeychain.swift`: browser authentication, local callback, and protected session storage.
- `macos/Services/BackendClient.swift` and `macos/Services/Telemetry.swift`: authenticated requests, generation, cancellation, and approved usage events.
- `macos/Tests/RendererChecks.cpp`: real renderer behavior and isolation checks, built separately against each SDK.
- `macos/Tests/WorkspaceChecks.swift`: meaningful save/restore, stale-response, private-editing, and frame lifecycle checks as those features land.

### Existing files used as references or shared assets

- `native/workspace.cpp`, `native/app.cpp`, and `spec-v2/APP.md`: accepted Windows interactions and product behavior.
- `native/overlay_ultralight.cpp` and `linux/src/scene_renderer.cpp`: renderer logic to adapt without Windows or Qt dependencies in the Mac interface.
- `linux/src/window.cpp`, `linux/src/store.cpp`, `linux/src/auth.cpp`, and `linux/src/annotation_geometry.h`: state transitions, persistence rules, backend flow, and geometry.
- `web/overlay.html`, `web/interactive-card.js`, `web/text-editor.html`, and `web/text-style.css`: shared documents and styling. Change only where the Mac bridge requires it, retaining existing platform behavior.
- `spec-v2/MACOS.md` and `spec-v2/README.md`: selected architecture and port status.

## Complexity and preparation limits

- **Reduced but unresolved uncertainty:** the shared message bridge works with isolated cards on both Linux SDK versions. The Intel Mac renderer still needs compilation and native runtime verification before full workspace work.
- **High integration complexity:** camera extension signing/activation, app-to-extension delivery, stream lifecycle, and recovery. A visible test picture is the first proof, not full calling-app acceptance.
- **High interface effort:** preview hit testing, focus, input methods, proportional scaling, and private text edits matching completed output. Native UI does not eliminate that work.
- **Moderate complexity:** webcam capture/recovery, saving, authentication/Keychain, AI request lifecycle, and packaging. Existing behavior and protocols provide concrete references.
- **Small incremental architecture cost for Silicon after Intel, or Intel after Silicon:** the Mac source is shared, but renderer compatibility and physical runtime verification remain separate work on each processor family.

Source and build preparation can happen from Linux. A macOS build environment is needed during development to catch SDK, Swift, linker, and packaging errors before the user reaches the hardware. Without that, the first Mac session includes compiler and integration fixes as well as runtime verification. No elapsed-time estimate is set before the two technical proofs.

## References

- [SwiftUI and AppKit integration](https://developer.apple.com/tutorials/app-dev-training/integrating-appkit)
- [Camera extensions and supported deployment model](https://developer.apple.com/videos/play/wwdc2022/10022/)
- [Ultralight 2.0 platform availability](https://ultralig.ht/blog/ultralight-2-0-out-now)
- [Ultralight 1.4 platform expansion](https://ultralig.ht/blog/ultralight-1-4-out-now)
- [Ultralight 1.4 JavaScript evaluation API](https://ultralig.ht/api/cpp/1_4_0/classultralight_1_1_view-members)
- [System extension distribution requirements](https://developer.apple.com/documentation/systemextensions)
- [App-to-extension sink streams](https://developer.apple.com/videos/play/wwdc2022/10022/?time=590)

Unattended workspace increment prepared October 16, 2026: adds pen, line, arrow, rectangle, ellipse, and multiline text annotations, whole-object erasing, confirmed clear, bounded movement/resizing, and private drafts. Committed annotation pixels use the production compositor; native selection and text controls remain in the preview view. Schema 3 preserves annotations and prompt drafts while accepting legacy workspace documents. Card stacking now supports both stepwise and end-to-end changes. One previous content version survives reopening and is consumed on restoration; revision tokens reject stale replacement results. The app snapshots and unloads card renderers when neither preview nor camera consumers need frames, and warns before quitting an active camera.

The signed Intel build, workspace/version recovery fixtures, annotation geometry/pixel/text fixtures, image checks, and actual Ultralight checks pass. A short synthetic/live-webcam regression also passed (`d2c0bcaf-0bd1-4836-9408-a7689018bfbb`), before the final library/lifecycle edits. Latest workspace build debug-library SHA-256: `306fb0c4af2df6949f8732247bf51c7723cbb3d5a11db6ec133e7d9eed8085f4`. These are automated results, not user acceptance of the native annotation editor, minimized-window lifecycle, or meeting-app appearance. The Applications copy and activated extension were not replaced for this increment.

Account and AI increment prepared October 17, 2026: native Google sign-in uses PKCE, a bounded one-use loopback callback, Firebase exchange/renewal, verified backend account access, and a dedicated macOS Keychain item. The workspace is created after authentication and a fresh picture from the selected webcam. Sign-out/expired access stops capture and delivery, preserves local work, and cancels pending generation. The sidebar now creates/revises AI cards through the existing service, retains local image references, validates responses and request identity, rejects changed/deleted revision targets, and offers explicit cancellation. There is no automatic regeneration after a lost response. Sample-card creation is removed from the interface, and the workspace uses the requested dark appearance.

Auth fixtures passed for PKCE, actual loopback HTTP callback handling, renewal/account binding, cancellation, late renewal after sign-out, expiry, and secure-storage failure. Generation fixtures passed for creation/revision/backup, image-reference bounds, invalid output, allowance errors, and no retry after lost response. The production public client-config contract was checked without printing values or signing in. A signed Intel build and short real-webcam regression passed (`be13f25a-c100-413c-a356-e496f254a0e0`); tested debug-library SHA-256 `0aa5ff65a5387109709970f2804e6dd009087fcfeb51dd6b977d1b632bbb815c`. The host source version is advanced to 0.4.0/build 9 for the next packaged checkpoint. Live Google consent, actual Keychain session reuse, paid generation, and native interface acceptance remain pending. No credentials or generated content were uploaded by these fixture tests, and no installed extension was replaced.

Telemetry increment prepared October 17, 2026: the Mac sends the four approved event families with `platform: macos`, bounded memory/batches/retries, stable event identities, and no prompts, names, filenames, email, paths, or image data. Creation is emitted after a successful workspace save. Preview usage follows actual composed card metadata. New extension 0.4.0/build 8 exposes normalized consuming-app labels, connection changes, and source-frame delivery timestamps; the host matches those timestamps against its own bounded frame ledger before claiming fresh output. An older extension remains compatible and does not fabricate confirmed output usage. Sign-out/quit ends usage and makes a bounded final telemetry attempt. Unused local-image cleanup retains assets referenced by either card versions or the recoverable workspace backup.

The signed Intel build and native suites pass. Telemetry fixtures cover fast reconnects, no events for unchanged frames, session end, stale/foreign frames, bounded buffering, retry identity/limits, and payload privacy; generated Mac events also pass the actual backend validator. Image cleanup fixtures verify current/previous references and eventual removal. Tested debug-library SHA-256: `b1f4fc7508e1f674e7593e0b7f40a15919d666ef258f6efa2c3ea6e699b127b2`. No real telemetry was submitted during fixtures. New extension activation, real consumer-label/delivery feedback, and authenticated PostHog arrival remain pending; installed extension 0.3.4/build 7 was left active.
