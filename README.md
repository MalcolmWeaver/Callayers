# Callayers

Fresh implementation of the camera, text editor, WinUI shell, live cards, and OpenAI card generation in [mvp-dev-plan.md](mvp-dev-plan.md).
The previous implementation remains deleted; nothing here restores it.

## Build

Requires Windows 11, CMake 3.24+, Visual Studio 2022 C++ build tools, and Windows SDK 10.0.26100.0 or newer. The build prepares pinned Windows App SDK 1.6.250602001 and WebView2 SDK 1.0.2651.64 from the local NuGet cache or downloads them from NuGet. C++/WinRT projections are generated into `build/generated`; no generated headers are checked in.

```powershell
.\scripts\build.ps1
```

Outputs are in `build\Release`: `Callayers.exe`, `CallayersCamera.dll`, and `CameraProbe.exe`.
The build script runs protocol/color conversion, media-source lifecycle, editor document/rendering, native editor interaction, shell editor-command, and AI request-targeting tests.
The lifecycle test also releases the activation helper before using the source, guarding against premature shutdown during Windows startup.

## Register and run

Install the shell's Windows App Runtime prerequisites once, as your normal desktop user:

```powershell
.\scripts\install-winui-runtime.ps1
```

The script downloads Microsoft's signed installer and validates its signature before running it. Having the framework package alone is insufficient; the runtime support packages must also be present.

The Windows camera service must load the media-source DLL from a stable location with machine-wide COM registration. In **Administrator PowerShell**:

```powershell
.\scripts\register-camera.ps1
```

This copies a versioned development DLL into `%ProgramFiles%\Callayers Development` and registers only the Callayers CLSID `{A487A33B-6EC3-43F1-A894-CA2D937619F0}`. Re-run after changing the DLL. No kernel driver or other camera registration is modified.

Run `build\Release\Callayers.exe` as your normal user. Choose **Test pattern** or a webcam. The virtual camera starts automatically; **Retry camera** is available if activation fails. Select **Callayers (Windows Virtual Camera)** in a meeting app; Windows controls the appended device-name suffix.

The native WinUI 3 shell is organized around a left-hand card library and a camera stage. **Create with AI** opens the sidebar composer; card titles select a card for revision, and independent checkboxes put cards on the stage. Camera controls and file actions stay in the header. **Annotate** reveals a compact, Preview-inspired markup toolbar above the stage.
Launch with `--webcam --publish` to select the first available webcam and start output immediately.

- Moving bar: active test pattern.
- Live image: selected webcam, letterboxed to 16:9 without stretching or intentional mirroring.
- Gray pause symbol: no fresh producer frame, including after quitting the runtime.
- A webcam failure is reported in the window; selecting the webcam again retries it.
- Closing the editor hides it to the system tray; reopening Callayers restores the editor. Use **Quit Callayers** in the tray menu to exit (with the usual unsaved-deck prompt).
- The physical webcam is acquired only while the editor preview is visible or at least one virtual-camera stream is running. Minimizing also removes preview demand. Closing the last consumer releases the webcam; restoring the preview or opening a camera stream reacquires it.
- With no consumers, composition and browser-frame capture stop. Browser documents stay alive, so card scripts/timers retain their existing behavior.
- The registered virtual device stays available after quitting and supplies idle frames. Selecting it does not launch an exited runtime; open Callayers first.
- Stream demand uses persistent local pipe connections, released on stream stop/shutdown or process death and reconnected after runtime restart. Update the registered camera DLL when installing this change. Older source DLLs are supported through ongoing frame requests, with a one-second idle grace period after the last request.

Camera creation persists for the current user. Registration of the source DLL is machine-wide. The development producer is single-instance per machine, with local pipe access limited to its user, service identities, and administrators. This is a development build, not a signed release installer.

## Cards and live annotation

### First-run setup

The sidebar guides new users through three steps beside the live preview:

1. Choose a camera, then **Next**. Permissions and retry controls appear only when needed.
2. Enter a name and optional second line, then **Create name tag**. This is a fixed transparent template, with no AI request or API key. It appears on camera immediately; edits update the same card. Choose **Next** to continue.
3. Open Meet, Zoom, or Teams and select **Callayers (Windows Virtual Camera)** in camera settings. Windows appends the virtual-camera suffix to our registered friendly name, **Callayers**. **Next** becomes available after acknowledged fresh video delivery; **I'll do this later** skips connection testing.

Completion is remembered for the Windows user. **Use in a call** reopens the connection guide. The workspace distinguishes camera availability from recent live-frame delivery; a local preview alone does not confirm a connection. Delivery confirms a camera consumer, not meeting participation or a particular calling app. Name tags are regular cards saved with the deck; use Save to keep them.

- **Create with AI** opens the sidebar composer. Describe a card and click **Generate card** (or Ctrl+Enter). Enter alone adds a new line.
- Cards appear as compact title rows. Click a title to open its AI editor; double-click to rename it. Enter or clicking away commits the title, Escape cancels. The card menu also offers Rename for keyboard users.
- Each row has an independent **Show on camera** checkbox. Check multiple cards to show them together; uncheck any card to hide just that one. **Hide all** clears the stage.
- New cards remain hidden until checked. Revisions to checked cards appear on camera automatically when generation finishes; other checked cards remain visible. Revisions to unchecked cards stay hidden. Undo/Redo also updates checked cards automatically.
- Click a card title to reveal its local editing handles. Drag the **Move** handle above the card to position it; corner/side handles resize it. Inside the card, clicks and drags go to its HTML controls. Card menu actions offer Move up, Move down, and Delete. Sidebar order is also stacking order: later cards appear above earlier cards.
- **Save / Ctrl+S**, **Save as / Ctrl+Shift+S**, and **Open / Ctrl+O** manage `.callayers.json` decks. A deck holds up to 12 cards. Reopening starts with all cards hidden.
- AI configuration is available under **File → Settings…**, outside the everyday workspace.

### Images are cards

Use **Import image**, drop image files onto the stage, or focus the stage and paste with Ctrl+V. Each image becomes an HTML card containing an embedded PNG asset. It stays private until checked, and uses the same selection, AI revision, ordering, deletion, and save flow as other cards.

Ask AI to add a title, arrange text beside the image, crop its presentation, or change the background. This edits the HTML layout, not pixels or text baked into the image. Local image pixels are not uploaded to OpenAI; revision supplies HTML plus asset IDs, filenames/descriptions, and dimensions.

Images retain transparency and orientation normalization. Large images are reduced to fit the card's 1 MiB encoded-asset budget. The embedded asset travels with the saved deck; the original source file is not modified. PNG/JPEG/BMP/GIF and installed Windows image codecs remain supported. Importing a batch either adds every image or leaves the deck unchanged.

### Optional markup toolbar

**Annotate** shows Select, Pen, Shapes, Text, Color, Size, Undo, Redo, and Clear. Shapes offers Arrow, Rectangle, and Ellipse with transparent interiors. The color swatch opens a floating panel with standard swatches, a spectrum, and hex input; the camera and cards remain visible without a dark backdrop; Apply commits the choice and Cancel leaves it unchanged. Color is shared across annotation tools; Small/Medium/Large means stroke width or text size. There are no separate image, signature, note, eraser, laser, font-family, or fill controls.

- Pen (**P**): drag to draw; click for a dot.
- Shapes: choose a shape and drag from one corner/end to the other.
- Text (**T**): click the stage and type. Ctrl+Enter commits; Escape cancels. Double-click existing text or press F2 to edit again.
- Select: click a stroke/shape outline or text, then drag to move it. Handles resize it. Delete removes the selection. Annotation selection requires the toolbar to be open.
- Color and Size change the selected annotation, or the default for the next annotation.
- Undo/Redo support completed annotations, clear, and card edits. Native text fields retain their own typing undo.
- Hiding the toolbar exits drawing mode and leaves annotations visible. **Clear** removes annotations and can be undone.
- Changing which cards are shown or hiding all cards clears annotations and removes them from undo history. They cannot reappear on another card through Undo.

Annotations are temporary and are not saved with the deck. They do not modify card HTML. When opening an older deck, standalone images become image cards; saved text/ink becomes an **Imported annotations** HTML card so existing content is retained. Decks whose conversion would exceed 12 cards are left unopened with an explanation; the original file is untouched.

### Manual walkthrough for this redesign

The agent has not built, launched, or tested these changes, at the user's request. Close the app, build with `.\scripts\build.ps1 -SkipTests`, and launch `.\build\Release\Callayers.exe`.

1. Toggle Annotate and resize the window: the Cards heading and Create with AI button should remain fully visible. The size dropdown should align with the toolbar buttons.
2. Open the color swatch: a floating panel should show the palette, spectrum, hex input, and Apply/Cancel without scrolling or blacking out the camera behind it. Apply a custom color, then reopen and cancel another choice.
3. Create or import two cards. They should appear as compact title rows without previews or Play buttons. Single-click opens AI editing; double-click renames. Try Enter, Escape, and clicking elsewhere.
4. Check both cards, select each title, then drag its Move handle to position the cards apart. Confirm both appear in the receiving camera app. Unchecking either should leave the other visible.
5. Revise a checked card. Its content should update automatically when generation finishes, while other checked cards remain visible. Revise an unchecked card and confirm it stays hidden. Undo/Redo should update a checked card too.
6. Try annotation tools, move/resize, Undo/Redo, and Clear. Hiding the toolbar retains marks; changing card visibility clears them.
7. Save/reopen: names and card content should persist, annotations should not, and all checkboxes should start unchecked.

## AI cards (step 4)

To save the key in a persistent Windows user environment variable and select **GPT-6 Luna**, save/close Callayers and run:

```powershell
.\scripts\set-openai-key.ps1 -Launch
```

The command prompts with masked input, saves `OPENAI_API_KEY`, selects `gpt-6-luna`, and reopens Callayers with the webcam/output enabled. The key is not written into the repository or command history. User environment variables are not encrypted like Credential Manager. A key already saved through AI settings takes precedence. Existing terminals may retain their old environment; this command also updates its own process so the app it launches receives the new key immediately.

1. Open **File → Settings…**, paste your OpenAI API key, and save. The key is stored in Windows Credential Manager for your Windows user; it is never saved in a deck. Alternatively, set `OPENAI_API_KEY` before launching. A saved credential takes precedence over the environment variable.
2. Click **Create with AI**, describe a card in the sidebar, and choose **Generate card**. The default model is `gpt-6-luna`; the model is configurable in AI settings.
3. Select a card title, then click the prompt and describe a revision. **Update card** keeps its identity and placement; Undo restores its previous content in one action.
4. Choose **Create with AI** to generate another card. Check the card to put it on camera.

The app sends the prompt, any retrieved website text, and, for revision, the selected card's HTML/name/size to the OpenAI Responses API with `store:false`. Webcam frames and other cards are not sent. OpenAI API usage has separate billing from ChatGPT. The model setting is stored in `%LOCALAPPDATA%\Callayers\ai.json`.

### Use website text, styling, and media

Include one HTTPS URL or common bare website domain in the prompt, for example: **Summarize https://streamfog.com in three concise points on a transparent card.** A prompt such as **give me three lines from puppetbooth.com** also retrieves the website, using HTTPS. Callayers retrieves that page before contacting the model, removes scripts/styles, and supplies up to 24,000 characters of text. The model is instructed to summarize only that evidence, copy source wording for requests for lines or quotes, and include a plain-text source label. This also works when revising a selected card. For an unrecognized domain suffix, include the full `https://` URL. Generated wording is not independently fact-checked.

You can also ask **Make a Puppetbooth card with three lines from puppetbooth.com and its logo**, or **Keep this card's content but use the colors and typography from puppetbooth.com**.

The lightweight reader inspects inline CSS and up to two linked stylesheets, sending at most 2,400 characters of selected declarations. This is a style reference, not computed browser styling or an exact visual match. It downloads up to one recognizable logo from the supplied page, with a site icon as fallback; labeled inline SVG logos are supported. General photos and social preview images are excluded. Raster logos (PNG/JPEG/GIF and installed Windows image codecs) are normalized to PNG, at most 1024 pixels on the longest side; GIFs use their first frame. Self-contained static SVG logos are supported. Typography uses available fonts or fallbacks; font files, screenshots, audio/video, PDF extraction, JavaScript rendering, site crawling, and internet search are not included.

Image files stay local. The model receives image IDs, page-provided descriptions, and dimensions, not pixels or base64 data. It selects images using that metadata. The application resolves IDs when rendering; the saved deck embeds the selected images for offline use. Edits preserve those IDs and do not upload image bytes. Older cards with base64 images in their HTML are compacted before revision. Cards with assets or generation usage use deck version 5; versions 1–4 still open. Each card supports up to eight retained images across revisions, at most 1 MiB per encoded asset and 4 MiB in total, within the scene's existing 32 MiB embedded-media limit.

This is a single-page snapshot, not a website crawl or live embed. Retrieval uses no browser login, cookies, or page JavaScript; JavaScript-only pages, login walls, PDFs, blocked sites, and pages over 1 MiB may need pasted text instead. Linked stylesheet downloads are capped at 128 KiB each and image downloads at 4 MiB each, with up to eight image attempts and a shared 60-second retrieval deadline. Redirects and linked resources must use public HTTPS addresses. Fetch failures preserve the existing card and offer retry; optional media/style failures show a partial-results notice. Canceling during retrieval prevents the subsequent model call. HTTPS redirects are followed up to four times. Website content is treated as untrusted source material, not instructions.

Retrieval itself makes no OpenAI tool call. Page text, the compact style sample, and image metadata add input tokens; normal card-generation output charges still apply. Cards remain network-blocked and reopen offline. After generation, the status shows the last generation's input/output tokens and estimated USD cost, retained on save/reopen and undo/redo. Output includes reasoning tokens; cached reads and cache writes are accounted for separately. Estimates use GPT-6 Luna's published rates checked October 11, 2026, including known processing-tier and long-context multipliers. Other models or unknown usage/pricing show no invented dollar estimate. This is per-card last-call reporting, not account billing: canceled requests, discarded results, and earlier revisions can also incur charges.

Generated cards default to transparent backgrounds, readable white text, and periwinkle/mint accents. Their HTML, CSS, and JavaScript are saved inside the deck and reopen offline. External resources and network requests are blocked; generated assets must be inline or embedded.

Progress, cancellation, and retry appear beside the prompt. Cancel discards the result locally; an already submitted API request may still finish and incur usage. Existing content stays visible during generation and while its replacement loads. Deleting/changing the target, undoing its content, or opening another deck prevents a late response from overwriting newer work. Moving/resizing the target remains allowed.

The request uses the official [Responses structured-output format](https://developers.openai.com/api/docs/guides/structured-outputs?api-mode=responses). See the [repeatable evaluation prompts](docs/ai-card-evaluation.md) for visual and live-call acceptance checks.

## Live card rendering

- **Create with AI** opens the new-card composer. The `--demo-cards` development option adds two handcrafted samples to the sidebar; check cards to show them.
- Only checked cards are sent to the camera renderer. Revisions to checked cards appear automatically when ready.
- Native annotations stay above the card. Selection borders and handles remain local.
- Delete, Undo/Redo, Save, and Open work with cards; HTML and image assets are stored in the deck for offline use.

Requires the Microsoft Edge WebView2 Evergreen Runtime. A missing runtime or failed renderer is reported in the status bar. Card scripts run in isolated iframes without same-origin permission, native host objects, or device permissions. The page policy blocks external resources and network requests; embedded data/blob images are allowed. Moving/resizing changes iframe geometry without recreating its document.

For a quick sample scene:

```powershell
.\build\Release\Callayers.exe --demo-cards --publish
```

The browser is rendered on a separate thread into a transparent Windows composition visual. Windows Graphics Capture reads that visual directly, without a visible browser window or capture border. Its output is composed below native text and keeps running independently of the editor window's visibility. Final NV12 blending remains on the CPU.

## Verify

```powershell
# Enumerate available cameras.
.\build\Release\CameraProbe.exe

# Verify live frames through the Windows camera pipeline, while the app is running.
.\build\Release\CameraProbe.exe --camera 120 --expect-moving

# Verify idle output after closing the app.
.\build\Release\CameraProbe.exe --camera 30 --expect-idle

# Automated pattern, webcam, and same-reader restart checks.
.\scripts\verify-camera.ps1

# Live browser capture: transparency, script animation, style isolation,
# and moving/resizing without restarting the card.
.\build\Release\CardProbe.exe

# With the default demo cards and Test pattern selected, before moving them:
.\build\Release\CameraProbe.exe --camera 120 --expect-cards
# Repeat while the editor is covered and minimized.

# WinUI runtime/control startup and clean shutdown (normal desktop user).
.\scripts\verify-shell.ps1
```

The integration script briefly opens the physical webcam. It does not record, save, or upload its images. It must run as your normal desktop user, outside a restricted agent sandbox, after registration. A sandbox can deny the protected named pipe even when the normal application works.

For startup diagnosis, `CameraProbe.exe --trace` listens for Callayers lifecycle events for 20 seconds. This opt-in diagnostic reports method names, process IDs, and object addresses, not video or audio. If Windows still has an older source loaded after registration, close Callayers and camera readers and reopen them before retesting.

For the final acceptance check, join a real Zoom call from a second device. Verify the pattern, webcam, orientation, switch-away/switch-back, and app-close/restart behavior. Automated camera reads do not establish how a particular meeting app displays or caches the device.

## Architecture of this first slice

- `native/app.cpp`: native Win32 preview, webcam selection, asynchronous Media Foundation capture, pattern generation, producer transport, and virtual-camera start.
- `native/card_host.cpp`: isolated WebView2 host, transparent visual capture, browser lifetime, and card geometry updates.
- `native/card_probe.cpp`: live GPU/browser verification, run separately from the fast build tests.
- `native/ai_client.cpp`: asynchronous OpenAI requests, structured response validation, configurable model, and Windows credential storage.
- `native/ai_probe.cpp`: developer-only real API generation/revision checks; writes separate evaluation decks.
- `native/editor.h`: text/card model, coalesced document history, and validated versioned JSON save/load with atomic replacement.
- `native/text_renderer.h`: DirectWrite/Direct2D text rasterization and immutable text overlays blended into NV12 frames.
- `native/text_editor_ui.h`: native text editing, selection/dragging, handles, style controls, and file actions. Editor-only controls never enter the compositor.
- `native/winui_shell.h`: C++/WinRT WinUI 3 chrome hosted in XAML islands, command bindings, contextual controls, keyboard focus, and status. The native stage remains a sibling surface.
- `native/source.cpp`: COM activation, Media Foundation source and stream, 30-fps sample delivery, and idle fallback. Windows Frame Server loads this DLL.
- `native/common.h`: fixed 1920×1080 NV12 protocol, BT.709 conversion, aspect-ratio handling, and bounded pipe transfers.
- `native/probe.cpp`: frame tests, direct source activation/lifecycle test, device enumeration, and real Windows camera reader.

Preview and output use the same complete NV12 frames except during typing: the local preview omits the active text element so the native editing control can draw it over a buffered copy of the live background without doubling the text. The camera output continues using DirectWrite text and never includes selection or caret controls. Text boxes automatically fit their content height after typing, font changes, or width changes.

The initial transport is a local named pipe with bounded, acknowledged transfers, rather than shared-memory slots. A producer timestamp older than 750 ms is rejected; an absent producer yields a distinct static idle frame. The source schedules timestamps and frame pacing independently of the app.

The shell uses WinUI 3 controls around the retained native stage and text input surface. The planned D3D11 compositor and GPU conversion are **not implemented**: composition remains on the CPU, with DirectWrite/Direct2D text rasterization and a buffered GDI preview. Keep the protocol and virtual-camera boundary while introducing the planned GPU compositor. Profile before deciding whether to replace the pipe with shared memory.

## Remove development registration

Close Callayers and meeting-camera readers. As the user who created the camera:

```powershell
.\build\Release\CameraProbe.exe --remove
```

Then in Administrator PowerShell:

```powershell
.\scripts\unregister-camera.ps1
```

The script removes only the Callayers registration and development DLLs. If Windows still has a DLL loaded, removal reports the failure; close camera clients or reboot and retry. It does not stop shared Windows camera services.

## Interactive cards

Card contents are interactive by default, with no Select/Interact switch. Click a shown card's controls to type, toggle, or expand them. Select a card from its sidebar title (or click its contents) to reveal a **Move** handle above it and resize handles around it. These local handles take priority over HTML and never appear in the outgoing camera. Interior dragging remains available to text selection, sliders, and custom HTML controls.

Active annotation tools take priority over HTML. The annotation arrow returns to normal card interaction while allowing existing annotations to be selected. Escape releases HTML keyboard focus back to the canvas. Ordinary typing, navigation keys, Tab, text selection, plain-text paste, and browser field undo/redo are supported. Input-method composition and touch/pressure input are not implemented in this initial interaction path.

Open `examples/interactive-template.callayers.json`, check its card, then click directly into its fields. Try the topic and notes fields, checkbox, and collapsible. Save, reopen, and show the card again to inspect the restored values. An AI request such as “Make an editable agenda with a title, notes, checkbox, and a collapsible section” uses the same mechanism.

Card JSON contains an optional `state` object alongside HTML/assets. Decks containing state use version 6; versions 1–5 still load. State is limited to 64 KiB per card. Text fields, checkbox/radio values, select values, plaintext contenteditable regions, and `details.open` persist when marked with a stable `data-state-key` or `id`. Password/file inputs are excluded. Animations and hover effects are not saved. Revisions retain existing state; field identifiers must remain stable for values to carry over.

Custom scripts can call `window.callayers.getState()` and `window.callayers.setState({field: value})`, and listen for `callayers-state` when restoring custom UI. This is a JSON state bridge only; cards retain their sandbox and network restrictions. Save waits for live controls to report their latest values and reports a retryable error if an unresponsive card cannot finish. Arbitrary DOM mutations and JavaScript variables are not automatically serialized.

These interaction changes have not been built or visually verified by the implementing agent.
