# Full Callayers Mac App Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans or superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Build and install a native Apple Silicon Callayers app on this Mac that matches the main GitHub app's workspace and implements its account, creation, image, annotation, persistence, and video features.

**Architecture:** Continue the existing SwiftUI/AppKit Mac port on `build/macos-apple-silicon`, retaining the Ultralight 2 renderer and AVFoundation/Core Media I/O pipeline. Use the existing Google/Firebase/backend protocols and Keychain session storage. The Windows implementation supplies the interface and behavior reference; do not attempt to compile WinUI on macOS or replace the native interface with a website.

**Tech Stack:** Swift 5, SwiftUI, AppKit, Foundation URLSession, Network, Security/Keychain, Core Graphics, AVFoundation, existing Objective-C++/C++ bridges, Xcode 27, pinned arm64 Ultralight 2.0.0-beta.2.

**Spec:** `spec-v2/APP.md`, `spec-v2/MACOS.md` checkpoints 3–5, `spec-v2/AI.md`, `spec-v2/BACKEND.md`, `spec-v2/DATA.md`, and `spec-v2/FRAMES.md`. Follow the current Windows interface in `native/workspace.cpp`, not the outdated controls crossed out in the reference screenshot.

## Global Constraints

- Target this Apple M5 MacBook Air with native `arm64` binaries; retain the existing macOS 12.3 deployment target unless an unavoidable SDK requirement is demonstrated.
- Match the dark GitHub workspace: header, left card library and compact create/edit composer, large preview, optional annotation toolbar. Remove development sample-card controls and manual Save from the product interface.
- Require validated Google sign-in and a working selected webcam before opening the workspace. Never silently change the remembered webcam. Explicit sign-out stops capture and camera delivery and retains local cards.
- Use the real deployed API, defaulting to `https://callayers-v2-api-3fj7uh2a7a-uc.a.run.app`; retain a development service URL override. No embedded server provider credentials, fake AI, or bypass sign-in in the normal app.
- Preserve renderer isolation: opaque-origin card frames, denied network/filesystem/clipboard access, bounded validated state, and private native editing controls.
- Preserve existing project changes made through Xcode. Do not reset the project or scheme, modify `sources/`, revoke certificates, or change unrelated installed applications.
- Camera activation requires a paid Apple Developer team and valid local signing identity. Xcode currently exposes only a Personal Team. Build the app and extension locally, but do not claim virtual-camera activation or Meet/Zoom delivery until those prerequisites and runtime checks succeed.
- This request is a local app build. Public website changes, release publication, notarization, update hosting/feed deployment, and Intel acceptance are separate distribution work; do not publish or invent an available update service.
- Runtime acceptance belongs to the user under `AGENTS.md`; present short verification steps in chat, not a separate verification document.

## Review Focus

1. A canceled request, deleted card, changed account, or newer edit must prevent a late AI response from replacing current work.
2. Imported image references must survive revision/restoration and relaunch without sending image bytes in AI revision payloads or allowing arbitrary resource access.
3. Sign-out, expired/invalid credentials, camera disconnection, or invisible idle windows must stop appropriate work and clear stale output without deleting saved cards.
4. Text drafts, selection handles, and eraser highlights must stay private while completed annotations remain in delivered video.
5. Interrupted writes, old schema documents, invalid dimensions, and renderer timeouts must preserve prior saved work and produce actionable errors.

---

## Task 1: Complete the persistent card and workspace domain

**Files:** Modify `macos/App/WorkspaceModels.swift`, `WorkspaceStore.swift`, `WorkspaceModel.swift`, and `macos/Tests/WorkspaceChecks.swift`; create `macos/App/CardRevision.swift` and `macos/App/AnnotationModels.swift`.

**Interfaces:**
- Extend `WorkspaceCard` with content width/height, kind (`ai`/`image`), optional local image asset identity, one previous content version, and a revision token. `width`/`height` remain scaled placement dimensions.
- `CardContent` stores name, HTML, intrinsic width/height, and optional image identity; backup excludes placement/visibility. `CardRevision.apply(to:expectedRevision:content:)` rejects stale results, saves one prior content version, and resets incompatible interactive state.
- Extend `WorkspaceDocument` with completed annotations and unfinished composer text, with explicit backward decoding defaults for existing version 1 files.
- `WorkspaceStore` stores production data under `Application Support/Callayers/Workspace`; leave the development workspace untouched. Serialize validated writes off the video/UI loop and expose completion/failure.

- [ ] Add checks for version 1 decoding, new-schema round trips, invalid/missing asset identities, arbitrary valid card dimensions, duplicate IDs, finite bounded geometry, and rejected malformed backups.
- [ ] Run workspace checks against the unmodified implementation and observe failures for newly required behavior.
- [ ] Implement the schema and content revision operations, including backup consumption on restore and placement/visibility retention.
- [ ] Add interrupted-write and save-failure checks that prove the last successful document remains readable; ensure schema errors cannot overwrite unreadable work.
- [ ] Run existing and new workspace checks; review the diff and commit only this task's files.

## Task 2: Render arbitrary cards and safely import images

**Files:** Modify `macos/Renderer/Renderer.h`, `Renderer.cpp`, `RendererBridge.h`, `RendererBridge.mm`, `macos/Resources/scene-host.js`, `macos/App/WorkspaceModel.swift`, and `macos/Tests/RendererChecks.cpp`; create `macos/App/ImageImport.swift` and `macos/Tests/ImageChecks.swift`.

**Interfaces:**
- Extend `Renderer::loadCard` and `CLRendererBridge.loadCard` with intrinsic content dimensions while preserving defaults for existing checks.
- `ImageImport.prepare(url: URL) throws -> ImportedImage` rejects files over 32 MiB or decoded images over 24 million pixels, downsizes within 1280×720 without upscaling, and reduces the encoded PNG to at most 1 MiB as in `native/image_import.cpp`. It produces a local image asset and safe card HTML and never retains arbitrary filesystem access in the card.
- Persist HTML with `callayers-image:local`; resolve only the selected card's validated image to an embedded data URL at render time. Revision payloads retain the local reference.
- Derive frame cropping, preview hit testing, and proportional scale from intrinsic dimensions, replacing fixed 640×400 assumptions.

- [ ] Add failing renderer tests for two different intrinsic sizes, proportional resize preserving state, correct crop/alpha, and denied undeclared resource access.
- [ ] Add image checks for invalid files, corrupt/oversized images, safe filename display, PNG/JPEG orientation, image reloading, and unresolved asset failures.
- [ ] Implement native file import and preview drag/drop through the same validator; save successfully before reporting content creation.
- [ ] Run the renderer observer checks and image/workspace checks; confirm network/filesystem isolation remains enforced.
- [ ] Review and commit this task's files.

## Task 3: Add real Google sign-in and protected sessions

**Files:** Create `macos/Services/BackendClient.swift`, `AuthController.swift`, `AuthCallback.swift`, `SessionKeychain.swift`, `macos/App/SignInView.swift`, and `macos/Tests/AuthChecks.swift`; modify `CallayersApp.swift` and `VideoModel.swift`.

**Interfaces:**
- `BackendClient` provides async bounded JSON/form requests with injected `URLSession`, TLS-required service roots (loopback allowed for tests), rejected redirects, and typed HTTP errors.
- `AuthCallback` owns a temporary `127.0.0.1` listener, random state and PKCE verifier/challenge; accepts exactly one matching `/callback` response and supports timeout/cancel.
- `SessionKeychain` saves service/project/account-bound Firebase refresh credentials using Security APIs, never UserDefaults or workspace files.
- `AuthController: ObservableObject` exposes state, email/account identity, `signIn()`, `cancel()`, `signOut()`, and `validToken() async throws -> String`. Restore validates `/client-config`, Firebase renewal identity, and `/account` before unlocking.
- `VideoModel.setSessionAllowed(_ allowed: Bool)` gates capture, frame composition, and camera output independently of preview visibility.

- [ ] Add failing tests for mismatched/duplicate callback state, malformed URLs, cancellation/replay, redirect rejection, wrong account/project renewal, expired credentials, and Keychain error handling via injected storage.
- [ ] Implement the existing desktop Google authorization-code/PKCE → Google token → Firebase signInWithIdp → refresh/account protocol, using bounded async networking off the frame loop.
- [ ] Add restore, token renewal, sign-out clearing, and session generation checks so canceled/old sign-ins cannot unlock the app.
- [ ] Add user-facing sign-in and camera-ready screens; transient service outages must not interrupt an already permitted call, while confirmed invalid credentials/sign-out clear output.
- [ ] Run auth checks with fake HTTP responses and existing video/workspace checks. Real sign-in is a later user check; do not fabricate an authenticated session.
- [ ] Review and commit this task's files.

## Task 4: Implement AI creation, revision, attachments, and allowance handling

**Files:** Create `macos/Services/GenerationController.swift`, `macos/App/CardComposerView.swift`, `macos/Tests/GenerationChecks.swift`; modify `WorkspaceModel.swift`, `CardRevision.swift`, and backend DTOs in `BackendClient.swift`.

**Interfaces:**
- `GenerationController` sends `/generate` with a new UUID v4, prompt, optional pasted-text attachments, and exact current-card DTO (`name,html,width,height,imageIds`).
- Expose progress, cancel, actionable error, and allowance/reset state. Cancellation invalidates the local request result, without pretending to reverse a server reservation.
- Enforce at most eight text sources, source names of 1–80 characters, 100000 UTF-8 bytes total prompt/source text, and 131072 UTF-8 bytes of returned HTML. New generated cards have names of 1–80 characters and integer content dimensions of 180–1280 by 100–720; image revisions retain the backend's permitted smaller dimensions. Preserve a locally renamed card name during revision.
- Apply a response only when request ID, account/session, card ID, and captured content revision still match. New cards and revised cards save before success events.
- Composer supports new-card and selected-card editing, pasted text and text file attachments under existing backend limits; imported image pixels remain local.

- [ ] Add failing request/response tests for exact DTO fields, size/dimension constraints, invalid response HTML/name, duplicate request IDs, allowance errors, invalid sessions, and source text limits.
- [ ] Test cancellation, deletion, account change, and a newer edit during an in-flight request; none may apply stale content or replace the previous-version backup.
- [ ] Implement create/revise flows and restore previous version; keep old content visible throughout a request and leave it unchanged after failure/cancellation.
- [ ] Ensure daily exhaustion disables only AI requests, keeps local editing/video available, and displays the next UTC reset in local time without model/cost/token details.
- [ ] Run generation, revision, persistence, and renderer checks; review and commit.

## Task 5: Complete annotations and private native editing

**Files:** Create `macos/App/AnnotationController.swift`, `AnnotationToolbar.swift`, `AnnotationTextEditor.swift`, `macos/Video/AnnotationRenderer.swift`, and `macos/Tests/AnnotationChecks.swift`; modify `PreviewView.swift`, `WorkspaceModel.swift`, and composition layer assembly.

**Interfaces:**
- `AnnotationController` exposes select, pen, rectangle, ellipse, line, arrow, text, eraser, color, size, and confirmed clear operations over completed `WorkspaceDocument` annotations. Preserve the existing limits of 300 annotations and 65536 total ink points, stroke sizes 3/6/10, and text sizes 24/36/54 logical units.
- `AnnotationRenderer.render(completed:)` returns a transparent 1920×1080 layer using the same logical 1280×720 coordinates as the preview.
- Native pointer routing prioritizes active annotation tools/handles, then card handles, then HTML. Selection permits move/resize; double-click starts native text editing.
- Draft text and decorations are private AppKit views. Until commit, the completed previous annotation stays in the composed output. Enter inserts a newline, Command-Enter commits, Escape cancels; clicking away commits nonempty text.

- [ ] Add failing geometry/composition tests for pen/shape/text placement, selected annotation movement, proportional resize, object eraser hits, and clearing without removing cards.
- [ ] Test identical committed text metrics before/during editing and verify draft text, handles, and eraser highlights never enter the output layer.
- [ ] Implement editing and persist completed mutations; eraser/clear keep no undo history, and canceled text edits do not alter completed output.
- [ ] Run annotation and pixel composition tests, including hide/show cards retaining annotations and saved annotation reopening.
- [ ] Review and commit this task's files.

## Task 6: Match the main GitHub interface and lifecycle

**Files:** Modify `macos/App/WorkspaceView.swift`, `CallayersApp.swift`, `CameraPickerView.swift`, `PreviewView.swift`, `WorkspaceModel.swift`, and `VideoModel.swift`; add `macos/App/Theme.swift` and `macos/App/CameraRecoveryView.swift`.

**Interfaces:**
- Present the dark GitHub layout with logo/header/File/Annotate/webcam/status, a left library with Create with AI, Import image, Hide all, search, full-width card rows, and the compact composer; preview takes remaining space.
- Card name selection and visibility remain independent; row actions include rename, stacking, confirmed delete, and restore previous version. No Add counter, manual Save, deck title, resource numbers, or raw technical errors in product flows.
- App route is restoring → signed out → camera selection/recovery → workspace. Saved cards survive session changes and webcam disconnects.
- Hidden cards and idle windows checkpoint state before suspending renderers. Minimizing with active output preserves rendering; close/quit warns if video is in use and awaits safe save completion.

- [ ] Add state-transition checks for sign-out, camera unplug/retry, no silent device substitution, minimized active/idle states, and failed-save quit cancellation.
- [ ] Implement the interface using existing native controls and the current Windows implementation as the source of truth; bundle the existing Callayers mark.
- [ ] Check keyboard navigation, focus release outside cards, in-place rename Enter/Escape/focus loss, text shortcuts and IME composition, minimum window size, and long card names.
- [ ] Run app typecheck/build and existing interaction/storage/video checks; prepare user runtime checks for the final build in chat.
- [ ] Review and commit this task's files.

## Task 7: Add bounded approved telemetry

**Files:** Create `macos/Services/Telemetry.swift` and `macos/Tests/TelemetryChecks.swift`; modify session, save success, and video transition hooks; extend `VirtualCameraClient` only as needed to report confirmed delivery and best-effort normalized consumer identity.

**Interfaces:**
- Use the exact four event families and envelopes from `spec-v2/DATA.md`, with `platform: macos`, UUID v4 IDs, original timestamps, and account-bound sessions.
- Queue at most 128 events, batch at most 32 after two seconds/full batch, at most three attempts with two-/ten-second delays, and discard events older than five minutes.
- Never include prompts, content, names, filenames, text, pixels, paths, PIDs, or browsing details. Unknown consumer identity remains `unknown`, never an inferred meeting service.
- Count creation after successful save; report applied card usage from actual rendered/delivered frames rather than selection. Delivery failure cannot interrupt auth, editing, saving, or video.

- [ ] Add failing envelope/privacy, queue-pressure, retry/dedup identity, expiry, account-switch and save-failure suppression tests.
- [ ] Implement async best-effort delivery through the authenticated backend client and transition-based event hooks.
- [ ] Verify hide/delete/minimize/output-stop ends the appropriate destination without per-frame network events or invented successful output.
- [ ] Run telemetry tests and existing backend telemetry contract tests; review and commit.

## Task 8: Produce and install the complete local app

**Files:** Modify `macos/Callayers.xcodeproj/project.pbxproj`, scheme, `macos/Config/AppInfo.plist`, `scripts/build-macos.sh`, `scripts/bundle-macos-renderer.sh`; create `scripts/package-macos.sh`; update `spec-v2/MACOS.md` to reflect only verified outcomes.

**Interfaces:**
- Build `Callayers.app` version 0.4.0/build 9 with the complete source/resources and bundled camera extension. Preserve current bundle identifiers and producer authentication identity unless a deliberate compatible migration is included.
- Existing host SDK acquisition, checksum validation, local signatures, and optional `--team TEAM_ID` signing remain supported.
- Extend build checks to the new domain, auth, generation, image, annotation, and telemetry checks. Keep writable module caches under the build directory; native video checks require execution outside the Codex filesystem sandbox.
- Package a self-contained Apple Silicon ZIP. Preserve a recoverable copy of any existing `/Applications/Callayers.app` before replacement; gracefully quit app instances and wait for save completion.

- [ ] Add the new files to appropriate Xcode targets and test invocations without dropping user changes to project/scheme settings.
- [ ] Run the full arm64 build and all meaningful checks, renderer isolation observer, backend tests, signature checks, dependency architecture checks, and ZIP integrity check.
- [ ] Conduct a fresh whole-diff review against APP/MACOS specs. Resolve actionable regressions and repeat affected checks.
- [ ] Package and install the local app, retaining a recoverable copy of the previous installed app; open the final app and provide grouped runtime checks in chat.
- [ ] If a paid team becomes available, run `bash scripts/build-macos.sh --arch arm64 --team TEAM_ID`, verify app/extension entitlements and matching team, install and request normal camera activation. Otherwise explicitly report camera activation as blocked; do not weaken macOS protections.
- [ ] Record build/test evidence in the existing Mac spec without claiming sign-in, AI generation, camera approval, or Meet/Zoom acceptance that the user has not performed. Commit the reviewed implementation and report exact remaining external blockers.

## Completion criteria

The installed native app matches the main GitHub workspace, implements real backend authentication and creation/revision, imports images, supports completed/private annotations, autosaves/restores content and values, and retains the existing camera pipeline. All relevant automated checks pass. User runtime observations establish actual sign-in, generation and camera behavior separately. Paid-team signing, system approval, and calling-app output remain explicit external requirements until verified; local build success is not a claim of a public distribution release.

## Local delivery outcome

Implemented and locally committed on `build/macos-apple-silicon`. The native arm64 0.4.0/build 9 package passes the full build, renderer isolation observer, all Mac feature/regression checks, 36 backend tests, signature checks and ZIP integrity. Independent whole-branch review and targeted fix rechecks found no remaining blocker among the reported findings. Installed at `/Applications/Callayers.app` and opened. The previous 0.1.0 app is preserved under `build/backups/Callayers-0.1.0-installed.app`. Real-account/visual/calling-app runtime acceptance remains with the user; paid developer signing is still required for camera extension activation. No shared branch push or public release was requested.
