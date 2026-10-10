# Callayers MVP development plan

## Goal and boundaries

Deliver a Windows 11 app that can be demonstrated at expos and meetups and then installed by early users. The demonstration combines a basic webcam background with AI cards, standalone text, images, drawing, laser pointing, output zoom, and reusable scenes.

The agreed scope and longer-term direction are in [project.md](project.md). This document specifies implementation order and verification; it does not assume any existing implementation has been inspected.

### Cuts applied to the MVP

- No movable/resizable/maskable camera layer or camera background effects.
- No separate editor zoom, dedicated Present mode, or speaker notes.
- No snapping, locking, or manipulation of completed ink as an object.
- One initial output format unless meeting-app compatibility requires another.
- Clear overlays replaces the proposed blank-scene hotkey.
- No direct editing inside generated cards, card interaction mode, reveal steps, or global hotkeys.
- Beauty and retouch are retained for later, not required for MVP completion.

Build a clean shell progressively. Do not defer all usability work until the last milestone, and do not build a second camera backend preemptively.

## 1. Establish the real camera path — complete

**Build**

- Minimal native window and stage preview.
- Moving test pattern sent through the Callayers virtual camera.
- Replace the pattern with a basic webcam background.
- Camera setup/status and an idle response when frames stop arriving.
- Start with one format, targeting 1080p at 30 fps.

**Verify**

- Select Callayers in a real Zoom call and observe it from a second device.
- Confirm ongoing movement, sensible orientation, and agreement with the stage.
- Switch away from Callayers and back.
- Close and restart the app while the meeting remains connected; confirm intended idle/recovery behavior instead of a misleading frozen live image.

**Exit:** the video path works outside the app. A successful stage preview alone is insufficient.

### Implementation status — October 10, 2026

Fresh milestone-1 code is in `native/`, with CMake build and setup scripts. The old implementation remains deleted.

- Release build passes. Protocol/color conversion and COM source lifecycle tests pass.
- Moving-pattern and live-webcam producers pass local cross-process frame-transfer checks: 60 complete frames, with 59–60 sequence changes.
- The direct media-source test passes activation, NV12 sample size, timestamps, pacing, stop/restart, and shutdown, including with live producer frames.
- The initial Windows device activation attempt exposed a missing activation interface. This was fixed and included in the direct test.
- Fixed a premature shutdown when Windows released the activation helper. The regression test releases that helper before requesting source characteristics and samples; it reproduced `MF_E_SHUTDOWN` before the fix and now passes.
- The corrected source is registered. Windows camera-reader integration passes: 120 moving-pattern frames in 3.98 seconds, 120 webcam frames in 4.01 seconds, and 30 idle frames after the producer closes.
- A single Windows reader remained connected for 420 frames (13.98 seconds) across producer close and restart, observing live output, 61 idle frames, and recovered live output.
- The user confirmed step 1 complete on October 10, 2026 and requested work on step 2. This records user acceptance; it does not claim an agent-observed Zoom call.
- Preview repainting now uses a persistent offscreen buffer and one final blit, suppresses background erasure, and updates status controls only when their values change. This addresses the reported white flashing. The rebuilt app was reopened with the webcam, and the registered output passed a further 60-frame live test; visual confirmation of the preview remains with the user.

Bootstrap differences: the first window uses Win32/GDI and CPU conversion, and frame transport uses a bounded local named pipe. The planned WinUI/D3D11 editor/compositor is not yet implemented. See [README.md](README.md) for build, registration, verification, and removal instructions.

## 2. Establish composition with standalone text

**Build**

- Text placement and direct editing.
- Select, move, corner-scale, side-reflow, and delete.
- Local selection controls and a compact tool strip.
- Basic undo and a versioned save/reopen format.

**Verify**

- Type a heading, move it, change wrapping, and undo the changes.
- Confirm a complete drag is one undo action.
- Save, close, and reopen; confirm text, style, and geometry match.
- Verify the remote participant sees text but no caret, selection handles, or editing controls. The local text editor is outside the video composition.

**Exit:** a small usable scene editor exists before adding generated content.

### Implementation status — October 10, 2026

Implemented; the user accepted the text-editor improvements and requested step 2.5:

- Text tool with click-to-place, double-click/F2 editing, live typing, and commit/cancel. The edit box and caret are local only.
- Selection and movement; corners scale text and its rectangle together, side handles change wrap width without changing font size.
- Font family, reference-pixel size, bold, color, alignment, and deletion.
- Undo/redo with each completed drag or typing session committed as one action.
- New/Open/Save, Ctrl+S and Ctrl+Shift+S, version-1 JSON files, atomic file replacement, and unsaved-change prompts.
- Immutable text overlays composed into the same NV12 frame used by preview and virtual-camera output. Selection adorners are painted only in the native editor.

Verification completed:

- All four automated suites pass: frame protocol, media-source lifecycle, editor document/rendering, and native editor interactions.
- Document checks cover Unicode/style/geometry save-load, replacement of an existing file, invalid versions and duplicate identities, undo/redo, and resize semantics.
- Interaction checks cover creating/typing, commit/cancel, selection without output changes, coalesced drag undo, and delete/undo.
- Registered Windows camera delivered 90 frames in 2.98 seconds with the expected text pixels present in every frame.

Follow-up improvements removed the opaque typing background and fitted text boxes to their content height, including after wrapping and font changes. Tests cover background preservation and automatic growth/shrinkage. Meeting-app visual confirmation remains a user check. The WinUI shell migration is tracked separately below.

## 2.5. Establish the modern WinUI 3 shell

**Build**

- Replace the temporary Win32 controls with a native WinUI 3 / C++/WinRT shell, retaining the working camera output, scene model, text rendering, and save/undo behavior.
- Establish the intended desktop layout: a clean top bar, large central stage, compact tool strip, and contextual text controls. Reserve room for the later card prompt and scene thumbnails without showing nonfunctional controls.
- Apply consistent typography, spacing, subtle borders, restrained rounding, and a periwinkle accent. Keep the interface familiar on Windows while giving Callayers its own identity.
- Introduce the Callayers frame-mark logo and wordmark, with a readable taskbar icon.
- Preserve direct text editing over the stage, with text boxes fitted to their content height.
- Keep camera status and save state clear without letting technical messages dominate the workspace.

**Verify**

- Review the running interface at typical laptop sizes and Windows display scaling settings; confirm controls remain readable and the stage gets the available space.
- Create, edit, move, resize, undo, save, and reopen text using the new shell. Confirm keyboard shortcuts, focus, and unsaved-change prompts still work.
- Confirm the live preview remains stable while typing, dragging, and resizing the window.
- Verify in a real meeting that the webcam and text still reach the audience, with no editor controls or selection handles in the outgoing video.
- Review the visual result with the user before adding the card interface.

**Exit:** the working camera and text editor run inside the modern, branded WinUI 3 shell. This is an explicit MVP milestone before cards; step 9 refines the established design rather than introducing it for the first time.

### Implementation status — October 10, 2026

- C++/WinRT WinUI 3 shell implemented using XAML islands around the retained native stage. The scene model, text compositor, virtual-camera source, and deck format are retained.
- Branded header, frame-mark logo and multi-resolution app icon; File/New/Open/Save/Save as; camera selection and start/stop; contextual text properties; undo/redo and compact status/footer.
- DPI-aware island/stage layout and keyboard routing between the toolbar and stage. Card generation and scene thumbnails are intentionally absent until their features work.
- Pinned build dependencies, generated C++ projections, and a signed Microsoft runtime setup script are included. The existing framework installation was missing the runtime support packages; those were installed for development verification.
- Verification: all five automated suites pass, including shell command bindings without the legacy toolbar. A separate runtime smoke check passes WinUI initialization, control creation, event-loop operation, and clean shutdown.
- Running-app checks cover text placement, direct typing, commit/cancel, contextual controls, camera selection, one-step toolbar undo, normal/maximized layouts, and reopening the user's saved deck through Ctrl+O. Ctrl+Enter no longer inserts an unwanted trailing blank line.
- Registered camera checks pass 90 live webcam frames in 3.01 seconds and 90 pattern-plus-heading frames in 3.01 seconds, with text detected in every frame of the latter check.
- Remaining acceptance: user review of the visual design, multiple Windows display-scaling settings, and a real meeting check. Those are not claimed by the automated camera reader or screenshots.

## 3. Prove live card rendering

**Build**

- One WebView2 with a transparent host page and a handcrafted card in a sandboxed iframe.
- A script-driven counter or animation inside the card.
- Capture and composite the card page above the webcam.
- Basic card movement and resizing without AI.

**Verify**

- The remote participant sees true transparency and advancing animation.
- Output continues while Callayers is covered or minimized.
- Moving the card does not reload it; resizing gives the document the new layout size.
- No browser/capture border appears in the video.
- Add a second small test card to confirm styles remain separate.

**Exit:** live browser content reaches the actual camera output. If this fails, resolve the capture approach before building the AI experience around it. A still-image workaround is not a passing animation test.

### Implementation status — October 10, 2026

Implemented and locally verified; real meeting acceptance remains:

- One native WebView2 composition controller with a transparent host and separate `sandbox="allow-scripts"` iframe per card. No same-origin grant, device permissions, native host objects, or external network resources. Inline scripts/styles and embedded data/blob images are supported.
- Windows Graphics Capture captures the browser's Windows composition visual directly. A separate STA keeps it independent of the visible editor, with fixed 1920×1080 coordinates so Windows display scaling does not offset the card from its handles.
- A purple animated timer card and a second green card with independent styling. Add through **Test card**; select, move, resize, delete, undo, save, and reopen. Geometry changes preserve the iframe and running script state. AI remains step 4.
- Cards compose above the webcam and below native text in both preview and outgoing video. Local text editing retains the live card background. Rounded corners remain transparent; native selection controls do not enter the browser visual.
- Deck version 2 stores card HTML and geometry; the loader retains version-1 text compatibility. Maximum 12 cards within the existing 100-element scene limit. Each completed card drag is one undo action.

Verification:

- Full Release build and all five regression suites pass. Document and interaction checks now cover mixed card/text save-load, identities, card geometry, one-step drag undo, deletion, and restoration.
- `CardProbe` passes with 320 captured frames and 110 changing animation samples; it checks transparent background, independent styling, move/resize without iframe restart using a script-state sentinel, and clearing removed cards.
- Registered Windows camera: 120 frames in 4.01 seconds, 109 card-region changes, and background transparency preserved in every frame.
- Minimized editor: 180 frames in 5.98 seconds, 138 card-region changes, and transparent background preserved in all 180 frames. The measurement samples the card itself, not the moving diagnostic background.
- Running-app checks confirm that card geometry and handles agree at the current Windows scaling, and that moving/resizing preserves the advancing timer and responsive layout.
- Saved and reopened the card deck after a full application restart, then verified live webcam output through the registered camera. WinUI startup and clean shutdown smoke check also passes.
- Remaining acceptance: view these cards from a second participant in a real meeting. Automated Windows camera reads do not claim a Zoom call or coverage across different GPUs and displays.

## 4. Add AI generation and revision

**Build**

- New card composer and selected-card revision.
- Responsive card template and shared theme guidance.
- Clear progress and failure feedback.
- Keep old content visible until replacement is ready.
- One-step undo for replacement.
- Bind each request to its original scene, card, and content revision.

**Verify**

- Use a fixed set of roughly ten prompts: agenda, comparison, numbered explanation, statistic, diagram, and simple animated content.
- Assess legibility from the remote video tile, layout fit, theme consistency, and generation wait time.
- Confirm selecting a card then clicking the prompt edits that card; explicit deselection creates a new card.
- Confirm replacement preserves placement and undo restores previous contents.
- Simulate a failed request; retain the existing card and offer retry.
- Delete or change the target while a request runs; its eventual response must not overwrite another card or newer work.
- Confirm generated resources follow the local/bundled asset policy.

**Exit:** the signature generate-and-revise demonstration works. Start informal user demos now; do not wait for full MVP completion.

## 5. Add multiple cards and standalone images

**Build**

- Card duplication, deletion, and ordering within the card layer.
- Image paste and drag-drop, selection, movement, resizing, and deletion.
- Foreground text/image ordering, respecting the layer policy in project.md.
- Bundle imported assets with saved decks.

**Verify**

- Arrange overlapping cards and manipulate one without changing its neighbors.
- Confirm duplicate cards receive independent identities and can be revised independently.
- Import an image, save, and reopen after its original file is moved or unavailable.
- Reopen a prepared scene without internet and confirm its existing assets display.
- Check local handles and contextual controls stay out of the outgoing video.

**Exit:** users can compose a useful scene from mixed material.

## 6. Add pen, eraser, and laser

**Build**

- Pen with a compact set of color and thickness choices.
- Persistent scene strokes and stroke undo.
- Eraser and fading laser.
- Keep ink above presentation content; no selection/transformation of whole drawings.

**Verify**

- Annotate cards and images in a live call.
- Erase and undo without affecting unrelated content.
- Save and reopen; persistent strokes return in place.
- Confirm laser marks fade and are absent from saved content.

**Exit:** the user can explain something spontaneously without generating a card.

## 7. Add output zoom and Clear overlays

**Build**

- Drag-to-zoom with an output rectangle constrained to the video aspect ratio.
- Animated transition and an obvious return-to-full-scene action.
- Map drawing and selection through the current output view.
- Reversible Clear overlays toggle showing the full webcam background, without deleting or saving destructive changes. Restoring overlays restores the prior composition and output view.

**Verify**

- The second participant sees the intended region enlarged without stretching.
- Draw and select at the correct location while zoomed; return home and confirm placement remains correct.
- Clear and restore overlays; verify cards, text, images, and ink remain intact.
- Confirm there is no separate editor-zoom behavior to confuse output zoom.

**Exit:** the presenter can direct attention and quickly return to a clear camera view.

## 8. Add reusable scenes

**Build**

- Scene thumbnails; add, duplicate, reorder, and delete with appropriate undo.
- Keyboard next/previous while the app has focus.
- Save/reopen the complete deck and shared theme.
- Predictable scene entry: begin at the full-scene view with no transient laser marks.

**Verify**

- Rehearse a multi-scene presentation in a real call.
- Duplicate a scene, edit the duplicate, and confirm the original is unchanged.
- Reorder, save, reopen, and confirm order and contents.
- Confirm typing in a text field does not accidentally trigger scene navigation.
- Confirm scene thumbnails never enter the outgoing frame.

**Exit:** a prepared presentation can be reused reliably.

## 9. Prepare for installation and live demos

**Build**

- Refine native desktop layout, contextual controls, and Callayers branding.
- Logo that works in the title area and at taskbar size.
- Clear camera setup, save status, retry actions, and missing-device feedback.
- Simple optional safe-area guide.
- Installer and required runtime setup for supported Windows machines.
- A small polished starter deck demonstrating the existing features.

**Verify**

- Install on a clean supported machine and select Callayers in Zoom.
- Observe a new user generate, revise, annotate, save, and reopen without developer assistance.
- Run an extended real-call rehearsal, including app restart, camera unavailability, and failed AI generation.
- Review card and text readability from the audience view at realistic video-tile size.
- Rehearse on the actual demo equipment with a saved deck available when internet generation is unavailable.
- Check distribution/uninstallation behavior and any installation permission requirements before handing the app to users.

**Exit:** the agreed MVP can be demonstrated and distributed to first users.

## Verification discipline

- Keep one reference deck and repeatable demo sequence throughout development.
- Verify rendering changes through actual outgoing video. A screenshot proves only the local UI.
- Use inexpensive manual checks for early interaction and visual work.
- Add focused automated checks where regressions lose work or target the wrong content: save/load, undo, card identity, request targeting, and scene duplication.
- Add a focused coordinate-transform check when zoom is introduced, because drawing and selection depend on it.
- Repeat broader checks when relevant changes or failures justify them; avoid building an exhaustive test framework ahead of the first demo.
- Keep failures recoverable: failed generation preserves work, missing assets get understandable feedback, and disconnected devices do not silently present stale live video.

## Feedback and scope control

Begin informal demonstrations after milestone 4. Record what viewers understand, what they ask to try, and where the flow stalls. Measure time to a useful card, revision quality, remote readability, and whether users naturally use text and pen.

Finish the committed MVP before expanding into speaker notes, Present mode, global hotkeys, editor zoom, or other later candidates. Beauty and retouch remain an explicit later opportunity for demo appeal and user acquisition, to evaluate after the core workflow is reliable.

The repeatable demo is: webcam -> type a heading -> generate a card -> revise it -> arrange an image -> annotate -> point with the laser -> zoom and return -> change scene -> clear and restore overlays.
