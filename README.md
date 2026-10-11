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

Run `build\Release\Callayers.exe` as your normal user. Choose **Test pattern** or a webcam, then click **Start camera**. Select **Callayers (Windows Virtual Camera)** in a meeting app; Windows controls the appended device-name suffix.

The native WinUI 3 shell has a Callayers frame mark, a compact top bar, contextual text properties, and a large stage. File actions are in **File**; save state and camera status remain visible. Only implemented tools are shown.
Launch with `--webcam --publish` to select the first available webcam and start output immediately.

- Moving bar: active test pattern.
- Live image: selected webcam, letterboxed to 16:9 without stretching or intentional mirroring.
- Gray pause symbol: no fresh producer frame, including after the app closes.
- A webcam failure is reported in the window; selecting the webcam again retries it.
- Closing the app stops physical webcam capture. The registered virtual device remains available and supplies its own idle frames.

Camera creation persists for the current user. Registration of the source DLL is machine-wide. The development producer is single-instance per machine, with local pipe access limited to its user, service identities, and administrators. This is a development build, not a signed release installer.

## Text editor

- Click **Text** (or press **T**), then click the stage to place text. Start typing to replace the placeholder.
- **Ctrl+Enter** commits; **Escape** cancels the editing session. Clicking outside the text box also commits.
- Double-click text or press **F2** on a selection to edit again. Enter creates a new line.
- Drag the text box to move it. Corner handles scale the box and font together; left/right middle handles change wrapping width only.
- Use the toolbar for font, size (1080p reference pixels), bold, color, and alignment. Click away from the size field to apply it.
- **Delete** removes the selected text. **Ctrl+Z** undoes; **Ctrl+Y** or **Ctrl+Shift+Z** redoes. While typing, native text-field undo works until the session is committed.
- **Save / Ctrl+S** saves a `.callayers.json` file. **Ctrl+Shift+S** saves under another name. **Open / Ctrl+O** reopens it.
- **File → New deck** clears the scene after offering to save changes. Closing the window also protects unsaved work.

Text appears in the video as it is typed, but its native editing box, caret, selection border, resize handles, and toolbar do not. The file format supports one scene and up to 100 text/card/image elements, including up to 12 cards and 32 images, plus up to 512 ink strokes (65,536 points total). Text-only decks remain version 1; decks containing cards use version 2; decks with ink use version 3; embedded-image decks use version 4. All four reopen in this build. Multi-scene management comes later.

## Local images

- Click **Image** to choose a local file, drag image files from Explorer onto the stage, or focus the stage and press **Ctrl+V** to paste a copied image or copied image files. Paste inside text/prompt fields retains normal text behavior.
- PNG, JPEG, BMP, and GIF are supported through Windows imaging codecs; TIFF/WebP depend on available codecs. Animated files import their first frame. Transparent PNGs retain transparency, and JPEG camera orientation is applied.
- Click and drag an image to move it. All resize handles preserve its proportions. Use **Delete image** or Delete; import, drag, resize, delete, and layer changes support undo.
- **To front / To back** arrange images and text within the foreground layer. Cards stay below this layer; drawing and laser remain above it.
- Images are normalized to embedded PNGs, with their longest dimension limited to 2048 pixels. Saving a deck includes the image data; reopening never needs the original file or a network connection. Local images are not sent to OpenAI.
- Each source file is limited to 20 MB and 40 million pixels; up to 16 files can be imported together. Embedded image data is capped at 32 MB of encoded text per scene. Invalid batches leave the scene unchanged.

## Pen, eraser, and laser

- Open **Draw** and choose **Pen**, **Eraser**, or **Laser**. With the stage focused, **P**, **E**, and **L** select them directly; prompt/text fields keep their normal typing behavior.
- Pen: drag on the stage to draw; click for a dot. Choose Mint, White, Periwinkle, Coral, or Yellow and Fine/Medium/Bold thickness. Ink appears above cards and text in the outgoing camera video.
- Each stroke is one undo action. Save/reopen retains its color, thickness, and normalized stage position. Completed drawings are not selectable or movable objects.
- Eraser: drag across ink to remove whole strokes. One eraser drag is one undo action and never removes text or cards.
- Laser: hold the mouse button and drag to point. The coral trail fades over 0.9 seconds, is never saved, and never enters undo history.
- **Escape** cancels an in-progress stroke/erase, clears the laser, and returns to Select. Switching tools or losing mouse capture finishes the current stroke.

Drawing is sampled in stage coordinates and repainted on the app's frame timer. The initial version uses fixed-width mouse/Windows-promoted pen input; pressure sensitivity is not implemented.

## AI cards (step 4)

To save the key in a persistent Windows user environment variable and select **GPT-6 Luna**, save/close Callayers and run:

```powershell
.\scripts\set-openai-key.ps1 -Launch
```

The command prompts with masked input, saves `OPENAI_API_KEY`, selects `gpt-6-luna`, and reopens Callayers with the webcam/output enabled. The key is not written into the repository or command history. User environment variables are not encrypted like Credential Manager. A key already saved through AI settings takes precedence. Existing terminals may retain their old environment; this command also updates its own process so the app it launches receives the new key immediately.

1. Open **AI settings**, paste your OpenAI API key, and save. The key is stored in Windows Credential Manager for your Windows user; it is never saved in a deck. Alternatively, set `OPENAI_API_KEY` before launching. A saved credential takes precedence over the environment variable.
2. Describe a card in the bottom prompt and choose **Generate card**. The default model is `gpt-6-luna`; the model is configurable in AI settings.
3. Select a card, then click the prompt and describe a revision. **Update card** keeps its identity and placement; Undo restores its previous content in one action.
4. Choose **New card** or deselect on the stage to generate another card.

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

- Use **+ Card** to focus the new-card composer. The `--demo-cards` development option adds two handcrafted animated samples.
- Select and drag a card to move it. Corner handles resize freely; side handles change width. The HTML responds to the new size, while its running animation continues.
- **Delete card**, Delete, Undo/Redo, Save, and Open work with cards. Card HTML is stored inside the deck, so these samples work offline.
- Native text stays above all cards. Selection borders and handles remain local.

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
