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

The shared renderer checks pass against the actual Linux builds of both SDK versions, including independent multi-card input/state, transparency, animation, isolation, rejected file/network access, and state validation. Both Mac SDK archives have been checked for version, checksum, architecture, and bundled resources. Swift syntax and Xcode references are checked on Linux; Swift compilation, workspace storage checks, and native UI/runtime verification remain pending on Mac. The renderer-per-visible-card composition is an initial preview implementation; native performance and the final video pipeline still need evaluation.

The Xcode project currently contains the development preview and `RendererChecks` targets. On a Mac with full Xcode 15.3 or newer selected and Python 3 available, run `bash scripts/build-macos.sh --open` from the repository root. The script acquires the host architecture's SDK, compiles the Foundation-only `WorkspaceChecks`, builds the app and renderer checks, verifies the local app signature, runs the checks on a matching host, and opens the preview. Workspace checks cover multi-card round trips, independent state, invalid records, damaged files, backup recovery, and failed writes. `--arch arm64` and `--arch x86_64` select explicit builds; execution checks require a matching host. The app is locally signed and uses its own `com.callayers.renderer-preview` identity. Developer ID distribution, camera entitlements, and notarization are not part of this checkpoint.

`.github/workflows/macos.yml` prepares separate Intel and Silicon build/check jobs; adding it locally does not run CI. Native input orientation, Swift/Objective-C++ compilation, AppKit event delivery, and packaged library loading remain Mac checks. IME composition and full editing shortcuts belong to the later workspace implementation. The user has confirmed Apple Developer membership; the team's signing identity and camera-extension provisioning will be configured at the camera checkpoint.

## Implementation plan

This is a substantial platform port: the interface, camera capture, camera extension, and system integration are new Mac implementations. Reuse the web assets, backend protocols, behavior, and suitable algorithms. Do not first refactor Windows and Linux into a universal core. Keep the new source under `macos/`, with one implementation shared between Intel and Silicon except for the renderer adapters and dependency selection.

### 1. Build and renderer proof

Prepare an Xcode project with a native app target and C++ renderer bridge, architecture-specific SDK acquisition, and a macOS build workflow. Compile both architectures in a macOS environment. Test representative transparent cards, animation, mouse and keyboard input, state restoration, and isolation. Resolve the Ultralight 1.4 frame scripting difference here.

This checkpoint establishes whether the chosen renderer split can support the existing product. An Intel adapter failure must be reported as that specific limitation; it does not automatically change the Silicon architecture. Linux source checks cannot substitute for compiling either Mac target.

### 2. Camera delivery proof

Add AVFoundation capture and a minimal camera extension. First deliver a generated test picture, then webcam plus one card, through the selectable Callayers camera. Prove signing, activation, consumer start/stop, frame timing, disconnect recovery, and unavailable output before investing in the full workspace.

Start by evaluating the supported Core Media I/O sink stream for app-to-extension delivery, with a source stream for calling apps. Keep the frame transport behind a small interface. Bound pending frames and drop superseded frames. Keep capture, composition, and card execution in the app; the extension delivers completed frames. Stream activity must drive resource lifecycle independently of whether a consuming application can be named.

Apple signing identities, bundle identifiers, and app-group configuration are needed to validate activation. Prepare the build configuration here; runtime activation and permissions remain Mac checks.

### 3. Native workspace and local persistence

Build the SwiftUI sidebar, camera selection, creation/editing controls, and annotation toolbar using the Windows UX as the reference. Embed an AppKit preview for interaction and private text editing. Add remembered cameras, safe local saves, imported images, card state, and previous-version restoration.

Keep the composition presented to calling apps distinct from private selection handles and annotation drafts. Verify text positioning, proportional resizing, keyboard focus, and saved-state behavior together with the preview. These interactions are a larger part of the interface work than the surrounding buttons and menus.

### 4. Account and backend integration

Add browser sign-in and callback handling, Keychain session storage, renewal and sign-out, AI creation/revision, cancellation, allowance handling, and telemetry with `platform: macos`. Use the existing API. The backend already accepts the Mac platform identifier; no Mac-specific backend service is planned.

Reuse current safeguards against late responses overwriting newer work. Separate connection detection from best-effort consumer identity; use an unknown identity when the actual calling app cannot be attributed, without interrupting video.

### 5. Packaged builds and runtime acceptance

Bundle architecture-matched renderer libraries, resources, web assets, license notices, and the camera extension. Produce separately signed and notarized Silicon and Intel downloads. Verify installation, sign-in, real calling-app output, minimized behavior, quit/reopen, and camera recovery on physical Macs. Keep verification instructions in chat, grouped by each small improvement.

An initial downloadable build is the release target. Automatic updates and public website publication are separate follow-ups. No cross-architecture performance or compatibility claim follows from an Intel-only runtime check.

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
