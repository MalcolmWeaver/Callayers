# Callayers

Fresh implementation of milestones 1 and 2 in [mvp-dev-plan.md](mvp-dev-plan.md).
The previous implementation remains deleted; nothing here restores it.

## Build

Requires Windows 11, CMake 3.24+, Visual Studio 2022 C++ build tools, and Windows SDK 10.0.26100.0 or newer. No downloaded packages are needed for this milestone.

```powershell
.\scripts\build.ps1
```

Outputs are in `build\Release`: `Callayers.exe`, `CallayersCamera.dll`, and `CameraProbe.exe`.
The build script runs protocol/color conversion, media-source lifecycle, editor document/rendering, and native editor interaction tests.
The lifecycle test also releases the activation helper before using the source, guarding against premature shutdown during Windows startup.

## Register and run

The Windows camera service must load the media-source DLL from a stable location with machine-wide COM registration. In **Administrator PowerShell**:

```powershell
.\scripts\register-camera.ps1
```

This copies a versioned development DLL into `%ProgramFiles%\Callayers Development` and registers only the Callayers CLSID `{A487A33B-6EC3-43F1-A894-CA2D937619F0}`. Re-run after changing the DLL. No kernel driver or other camera registration is modified.

Run `build\Release\Callayers.exe` as your normal user. Choose **Test pattern** or a webcam, then click **Start camera output**. Select **Callayers (Windows Virtual Camera)** in a meeting app; Windows controls the appended device-name suffix.

The window is an early native editor. It is not the final branded interface.
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
- **New** clears the scene after offering to save changes. Closing the window also protects unsaved work.

Text appears in the video as it is typed, but its native editing box, caret, selection border, resize handles, and toolbar do not. The initial file format supports one scene and up to 100 text elements; multi-scene management comes in a later milestone. Generated-card text editing remains separate future work.

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
```

The integration script briefly opens the physical webcam. It does not record, save, or upload its images. It must run as your normal desktop user, outside a restricted agent sandbox, after registration. A sandbox can deny the protected named pipe even when the normal application works.

For startup diagnosis, `CameraProbe.exe --trace` listens for Callayers lifecycle events for 20 seconds. This opt-in diagnostic reports method names, process IDs, and object addresses, not video or audio. If Windows still has an older source loaded after registration, close Callayers and camera readers and reopen them before retesting.

For the final acceptance check, join a real Zoom call from a second device. Verify the pattern, webcam, orientation, switch-away/switch-back, and app-close/restart behavior. Automated camera reads do not establish how a particular meeting app displays or caches the device.

## Architecture of this first slice

- `native/app.cpp`: native Win32 preview, webcam selection, asynchronous Media Foundation capture, pattern generation, producer transport, and virtual-camera start.
- `native/editor.h`: text model, coalesced document history, and validated versioned JSON save/load with atomic replacement.
- `native/text_renderer.h`: DirectWrite/Direct2D text rasterization and immutable text overlays blended into NV12 frames.
- `native/text_editor_ui.h`: native text editing, selection/dragging, handles, style controls, and file actions. Editor-only controls never enter the compositor.
- `native/source.cpp`: COM activation, Media Foundation source and stream, 30-fps sample delivery, and idle fallback. Windows Frame Server loads this DLL.
- `native/common.h`: fixed 1920×1080 NV12 protocol, BT.709 conversion, aspect-ratio handling, and bounded pipe transfers.
- `native/probe.cpp`: frame tests, direct source activation/lifecycle test, device enumeration, and real Windows camera reader.

Preview and output use the same complete NV12 frames except during typing: the local preview omits the active text element so the native editing control can draw it over a buffered copy of the live background without doubling the text. The camera output continues using DirectWrite text and never includes selection or caret controls. Text boxes automatically fit their content height after typing, font changes, or width changes.

The initial transport is a local named pipe with bounded, acknowledged transfers, rather than shared-memory slots. A producer timestamp older than 750 ms is rejected; an absent producer yields a distinct static idle frame. The source schedules timestamps and frame pacing independently of the app.

The planned WinUI 3 shell, D3D11 compositor, and GPU conversion are **not implemented in this bootstrap**. It uses a Win32 window, CPU frame composition, DirectWrite/Direct2D text rasterization, and a buffered GDI preview. Keep the protocol and virtual-camera boundary while introducing the planned GPU compositor. Profile before deciding whether to replace the pipe with shared memory.

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
