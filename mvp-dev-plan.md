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

## 1. Establish the real camera path

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
- The second-device Zoom check remains pending.
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
