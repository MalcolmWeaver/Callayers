# Callayers video foundation

Fresh Windows x64 implementation of step 1 in [the v2 specs](spec-v2/README.md). The first user verification setup is **USB Camera → Callayers → Google Meet in Brave**.

## Build and open

Requires Windows 10/11 x64, Visual Studio C++ Build Tools, CMake, and the Microsoft Edge WebView2 Runtime. The build script uses a pinned WebView2 SDK from the local NuGet cache or downloads that SDK from NuGet when absent.

```powershell
./scripts/build.ps1
```

Double-click **Start Callayers.cmd**. It registers the new DirectShow camera for the current Windows user and opens the development app. No administrator prompt is intended. Restart Brave after the first registration if its camera list was already open. Choose **Callayers** in Meet. Registration/launch are preparation for user verification, not proof of compatibility.

This is a development harness with fixed cards, a five-minute timer, an animated card, pen annotations, visibility toggles, and drag/resize. The uniquely named `USB Camera` is selected initially; otherwise choose explicitly. Retry preserves the device identity and never substitutes another webcam. Changes are temporary. This build intentionally has no sign-in, AI, saved-card library, or release installer and must not be distributed as the authenticated product.

The user performs every runtime check. The foundation and current resource cost have been accepted for the MVP; final evidence is preserved in Git. Follow [the delivery plan](spec-v2/README.md) and [feature verification guidance](AGENTS.md) for the next improvement. Compilation alone does not establish that video, transparency, minimize behavior, DirectShow discovery, or performance work.

## Structure

- `native/capture.*`: asynchronous Media Foundation webcam capture; latest image only.
- `native/overlay.*` and `web/overlay.html`: one transparent WebView2 page, sandboxed iframe per visible fixed card, parent-owned placement and annotations. Hiding removes the iframe; showing reloads it and resets its timer. Browser requests, downloads, device permissions, host objects and messages are denied.
- `native/app.cpp`: minimal native controls, background compositor, scaled preview and private selection handles.
- `native/frame_bridge.h`: fixed-size local shared memory, non-waiting copy lock, session identity, source freshness and consumer heartbeat.
- `native/camera_filter.cpp`: x64 DirectShow source, RGB24/RGB32 720p at 30 fps, paced delivery, neutral output generated in the consumer process.

PNG capture and CPU composition are deliberate initial choices. There is at most one outstanding browser capture, one pending encoded overlay, one decoded overlay and one shared output slot; slow work drops superseded images. Performance remains a user measurement, not an assumed advantage over the previous implementation. The independent offscreen browser host is kept rendering during an active minimized call. Idle minimization unloads the card documents and suspends the browser; resuming reloads the visible cards.

This harness uses an isolated WebView2 profile at `%LOCALAPPDATA%\Callayers\Foundation\WebView2` and a local `diagnostics/renderer.log` beside the executable. The log records startup stages, error codes and process-event kinds; it contains no images, card contents or URLs, and resets on each app launch. Writes run on a separate diagnostic thread. Existing saved cards are not read, migrated or deleted. The new camera uses its own COM identity, leaving older installed camera registrations alone. If two cameras named Callayers appear, report that before proceeding; do not guess which registration is active.

For resource diagnosis, `./scripts/build.ps1 -DiagnosticsBuild` compiles into `build/diagnostics-x64/Release`, preserving the current measurement build. After closing the current app, launch that folder's `Callayers.exe` directly; the already registered foundation camera uses the same frame bridge and need not be re-registered. Run the measurement helper with `-DiagnosticsBuild` to attribute the diagnostic executable correctly. Run only one Callayers instance at a time.

`diagnostics/resources.log` beside the running executable records the requested and negotiated native camera dimensions, subtype GUID and frame rate, RGB32 reader dimensions/stride, and five-second summaries for camera-frame copying, asynchronous overlay capture, PNG decoding, composition and frame publication. `camera_copy` rate reports delivered usable frames per second. Timings are elapsed wall time, not CPU attribution: asynchronous overlay capture includes waiting, camera copying excludes Media Foundation decoding/conversion, and publication excludes consumer-side DirectShow work. These diagnostics guide profiling; they do not establish a CPU bottleneck or acceptable video quality on their own. Logs reset at launch, contain no images or device identifiers, and write on a separate thread.

If the card renderer fails, Retry recreates it. GPU and utility process events that WebView2 automatically recovers no longer permanently disable composition; see Microsoft's [process-event guidance](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/process-related-events). Runtime recovery still requires user verification.

Close the app and release its camera in the calling app before rebuilding a loaded DLL. `scripts/unregister-camera.ps1` removes only this build's current-user registration. Registration points to the build output, so unregister before moving or deleting that folder.

Implementation references: [WebView2 transparency](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2controller2), [WebView2 capture](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2#capturepreview), [DirectShow live sources](https://learn.microsoft.com/en-us/windows/win32/directshow/live-sources), and [IAMPushSource](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nn-strmif-iampushsource).
