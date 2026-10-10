# Callayers project plan

## Purpose

Callayers is a Windows desktop presentation workspace that sends its composition to meeting apps through a virtual camera. Presenters can combine AI-generated cards, typed text, images, and drawing over a basic webcam background, and magnify details for their audience.

The initial business goal is a compelling expo or meetup demonstration that brings in first users and customers. The product must also be useful throughout an actual call: users can explain something immediately with text and pen, without waiting for AI.

The signature demonstration is: appear in a call, generate a useful visual, revise it with a prompt, arrange it, annotate it, zoom into a detail, and switch to a prepared scene.

This document records the agreed product direction, not an assessment of existing code. Implementation order and acceptance checks live in [mvp-dev-plan.md](mvp-dev-plan.md).

## Product concepts

- **Deck:** a saved collection of scenes, a shared theme, and image assets.
- **Scene:** a 16:9 composition containing cards, text, images, and ink over the webcam background.
- **Stage:** the preview of the composition being sent to the virtual camera.
- **Card:** a bounded visual with HTML, CSS, and JavaScript contents, created or revised with AI.
- **Text:** independently typed content placed directly on the scene; it is separate from text inside cards.
- **Ink:** persistent scene drawings. Laser marks are temporary and are not saved.
- **Output frame:** the part of the scene currently shown to the audience; output zoom changes this frame.

The webcam is a basic background, not a selectable scene element. Moving, resizing, masking, and replacing the camera background are outside the planned scope. Later beauty and retouch work is a separate, intentional exception for improving the presenter's appearance.

## MVP commitments

### Camera output

- Basic live webcam background and a simple fallback when it is unavailable.
- Windows 11 virtual camera that meeting apps can select normally.
- One initial output format, targeting 1080p at 30 fps; add formats only if compatibility testing requires them.
- Stage and outgoing video show the same composition.
- Prompts, selection controls, menus, thumbnails, and other editor UI never appear in the outgoing video.
- Clear camera status and a defined idle response when the app stops supplying video.

### AI cards

- Create a card from a prompt and revise the selected card with a follow-up prompt.
- Multiple cards with movement, resizing, duplication, deletion, and basic ordering.
- A shared theme supplies fonts, colors, and consistent visual guidance.
- Support scripted animation and changing content inside a card.
- Keep the previous card visible during generation; failure leaves it intact.
- Apply a successful replacement as one undoable action, preserving identity and placement.

### Typed text

- Click to place standalone text; double-click to edit it.
- Move, resize, and delete text.
- Corner resizing scales text; side resizing changes wrapping width without changing font size.
- Basic font, size, weight, color, and alignment controls.
- Save text and support undo for edits.

Direct typing into standalone text is an MVP feature. Direct editing of text inside an AI card is a later candidate.

### Drawing and pointing

- Pen strokes over the composition, with a small useful choice of color and thickness.
- Eraser, stroke undo, and persistence with the scene.
- A fading laser that is never saved.
- Completed ink stays in scene coordinates; selecting and transforming a whole drawing is deferred.

### Images and composition

- Standalone images through paste and drag-drop.
- Selection, movement, resizing, deletion, and basic ordering of cards, text, and images.
- Store imported assets with the deck.
- Undo for content and composition changes, with a drag treated as one action.
- Optional simple safe-area guide to help keep important content away from video-tile edges.

### Output zoom

- Drag a rectangle to magnify a scene region in the outgoing video.
- Animate the transition and provide a clear return-to-full-scene action.
- Fit the editor preview to the window; there is no separate editor zoom in the MVP.
- Keep selection and drawing aligned with the scene while the output is zoomed.

### Scenes and reuse

- Add, duplicate, reorder, and switch scenes.
- Compact scene thumbnails and keyboard next/previous navigation.
- Save and reopen decks with their theme and assets.
- Prepared content can display without an internet connection; new AI generation may require connectivity.
- A reversible **Clear overlays** toggle temporarily shows only the webcam. It does not erase content or alter the deck. Restoring overlays restores the composition.

## Card interaction contract

Callayers owns a card's identity, name, position, dimensions, and order. The card document owns its visual contents.

- Clicking a card selects it; dragging moves its container without reloading its document.
- Resizing changes the available layout space. Generated cards use a responsive template and sensible minimum dimensions.
- Clicking the prompt preserves selection. The prompt reads **Edit “Card name”**.
- Clicking empty stage space or pressing Escape clears selection. The prompt reads **New card**.
- An explicit New card action starts creation even if another card was selected.
- A generation request retains the scene and target it started with. A delayed response must not overwrite a deleted target or silently replace newer edits.
- Pointer input selects and manipulates cards in the MVP. Operating buttons or controls inside a card is a later interaction mode; scripts can still run autonomously.

## Technical direction

Use a native WinUI 3 / C++/WinRT shell, D3D11 composition, and Media Foundation virtual-camera output on Windows 11. These are planned choices to validate through early end-to-end proofs, not reasons to postpone a usable demo.

Use one WebView2 with a transparent host page containing one sandboxed iframe per card. Each iframe carries its own HTML, CSS, and JavaScript. Callayers positions the containers and captures the combined page for composition above the webcam.

The single card surface imposes a simple initial layer policy: webcam below cards, with standalone text, images, and ink above the card surface. Cards can be reordered among cards; text and images can be ordered within their supported foreground layer. Arbitrary interleaving of native elements between individual cards is not an MVP promise. Ink and laser remain above presentation content. Revisit this restriction only if real usage requires it.

Editor controls are drawn separately from the outgoing composition. Live capture must be proven with transparent, animated content while the app is covered or minimized. A static snapshot fallback can support static experiments but does not satisfy the animated-card milestone.

Cards may run scripts but have restricted host capabilities. Iframe sandboxing does not by itself define network policy: enforce resource access separately. The initial flow supplies imported local assets and bundled resources rather than depending on arbitrary remote requests. Generated cards do not access the webcam, microphone, neighboring documents, or unrestricted host functions.

Keep the AI provider/model configurable and evaluate it using representative card prompts, visual quality, responsiveness, and cost. Do not bake a dated price or latency assumption into product requirements.

## Desktop experience and branding

The interface should feel familiar on Windows while having its own identity. Use familiar window controls, keyboard behavior, and menus with restrained custom styling.

- **Top bar:** Callayers logo, deck name, save state, camera setup, and camera start/stop.
- **Center:** a large 16:9 stage.
- **Compact tool strip:** Select, Text, Card, Draw, and Zoom. Draw includes pen, eraser, and laser options.
- **Below the stage:** a persistent prompt composer labeled New card or Edit “Card name”, with image attachment and Generate/Update.
- **Bottom:** scene thumbnails and an add-scene action.
- **Contextual controls:** only the properties relevant to the selection.
- **Always accessible:** return from output zoom and Clear overlays.

No permanent layers/properties sidebar or separate Present mode is required for the MVP. Generation feedback stays near the prompt and does not cover the composition.

Initial visual direction: warm charcoal, off-white text, subtle borders, modest rounding, and a periwinkle accent for selection and primary actions. Keep the chrome quiet so the content is prominent.

Logo direction: two offset rectangular frames forming a subtle C, paired with a clean Callayers wordmark. Evaluate the symbol in one color and at taskbar size before decorative treatment.

## Later candidates

These remain product possibilities, not commitments for the MVP:

- **Beauty and retouch:** explicitly retained as a potential live-demo attraction and reason to adopt the app. Explore after the core presentation workflow is reliable.
- Speaker notes and a dedicated Present mode.
- Reveal steps and global hotkeys while a meeting app has focus.
- Snapping, element locking, and separate editor zoom.
- Selecting, moving, and resizing completed ink.
- Direct editing of text inside cards and explicit card interaction mode.
- Additional output formats and broader meeting-app coverage.
- Local recording.
- Controlled URL asset import and live data sources, if demanded by users.

Lower-thirds, title cards, and simple charts should first be delivered as starter content using the existing card system, rather than separate editor subsystems.

## Explicit exclusions

No planned investment in:

- Editable camera placement, resizing, masking, or background effects/replacement.
- Freeform animation timeline.
- Real-time collaboration.
- Template marketplace.
- Embedded video.
- Meeting-platform SDK integrations; the virtual camera is the integration strategy.

Reconsider exclusions only when concrete user evidence justifies changing the product direction. Beauty and retouch remain on the later list despite the exclusion of camera composition tools.

## Success criteria

Technical success means the audience reliably sees what the presenter intends, while editor UI stays private. Product success means a prospective user understands the value from a short demo, repeats the workflow with their own material, and wants to use it in a real meeting.

Observe time to a useful card, common revision requests, remote-view readability, failures, spontaneous use of text and pen, and repeat use. Let those findings determine which later candidate earns development time.
