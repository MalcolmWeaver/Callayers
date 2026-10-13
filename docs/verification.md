# Step 1 — user verification

**Status: preview working, reported by the user; remaining verification pending.** The assistant builds and fixes compilation errors. The user performs runtime checks; the assistant walks through one at a time, records the user's observations and fixes failures. Do not infer a pass from compilation or from the existence of a feature in code.

Build handoff: Release x64 compilation and linking succeeded on October 13, 2026 with Visual Studio 2022 Build Tools and Windows SDK 10.0.26100.0. Source starts from `2fa3000` with the new foundation changes in the working tree. The assistant has not launched the app, registered the camera, opened Meet, performed runtime tests, or recorded performance results.

## Record the setup

| Item | Value |
| --- | --- |
| PC identifier / Windows version | Pending user report |
| Webcam | USB Camera, selected by the user |
| Calling app | Google Meet in Brave; Brave version pending |
| Build | Step 1 foundation, 0.2.0; record build date and source revision |
| Receiving view | Record the second participant/device used to see outgoing video |
| Display scale | Pending user report |

## 1. Open the foundation

Double-click `Start Callayers.cmd` at the repository root. It registers the development camera for this Windows user and opens Callayers. If Windows displays a runtime or registration error, report the message. It should select USB Camera when there is exactly one device with that name; otherwise choose it yourself.

Expected: live webcam, a name card, a moving green dot, a five-minute countdown, and a yellow annotation. Transparent card surroundings reveal the webcam. The camera retains its aspect ratio with bars if needed. No sign-in is expected in this local development harness.

**Stop here if the image does not appear, transparency is wrong, or the timer/dot is frozen.** Report what the preview shows. No later check should be marked passed to compensate.

After the first reported startup failure, the build now records local renderer diagnostics and Retry also recreates a failed card renderer. Reopen the rebuilt app and repeat this first check. If it still reports Cards unavailable, click Retry once and report the result; the assistant can read `build/x64/Release/diagnostics/renderer.log` directly. Do not reinstall WebView2 solely because the original generic error suggested it.

## 2. Receive video in Meet

Open Google Meet in Brave, permit camera access, and choose Callayers. Restart Brave if the camera list was open before first registration. If two entries have the same name, report this before choosing; the previous camera registration may still be installed.

Make a short call using another participant/device to view the received image. A local mirrored self-view alone is not sufficient to compare orientation. Keep that second device's microphone/speaker muted as appropriate to avoid feedback.

Expected: same content, proportions, orientation and placement as Callayers preview, with live animation and a running timer. Different frame timing is acceptable. There should be no steadily growing delay. Move a card with Select, resize it using its lower-right corner, toggle visibility, and draw with Pen. Editing outlines/handles must not appear to the receiver. Resize the app window; outgoing placement must stay unchanged.

## 3. Timer and visibility

Hide the timer card for a few seconds, then show it. Expected: it restarts at five minutes. Hide/show the animation; it restarts. Hiding cards leaves annotations intact. Clear annotations asks for confirmation; cancel retains them, confirm removes them.

Changes are intentionally not saved in step 1. Reopening restores the fixed demonstration scene and restarts its timers; saved workspace restoration belongs to a later milestone.

## 4. Minimize and reconnect

While Meet is receiving video, minimize Callayers for at least 15 seconds. Expected: moving dot, webcam and countdown remain live; minimizing during a call does not reset the timer.

Keep Callayers minimized and stop Meet's camera use, closing its preview/tab if needed so it actually releases the camera. Wait a few seconds. Expected: webcam capture stops and browser card work pauses. A camera LED is supporting evidence only; it is not a reliable requirement. If camera release or process activity is unclear, leave this result pending and ask the assistant to guide an appropriate measurement.

Start Meet's camera again while Callayers remains minimized. Expected: fresh video returns; the reloaded timer starts at five minutes. Restore the Callayers window and confirm its preview resumes.

## 5. Quit, crash and recovery

With Meet receiving video, close Callayers. Expected: a warning asks whether to stop video and quit. Cancel leaves the call running. Confirm stops live output and exits the app, with no tray mode. The source supplies a dark neutral picture, not an indefinitely repeated webcam image.

Reopen Callayers. Expected: fresh webcam and overlay output return. Then use Task Manager to end **Callayers.exe** while Meet is receiving video. Expected: the camera source supplies neutral output after at most about one second of stale publication. The browser may retain a cached image independently; if that happens, record the observation and investigate instead of calling the check passed.

Reopen again. Disconnect USB Camera. Expected: output becomes unavailable, no other camera is selected, and the app offers Retry. Reconnect USB Camera and choose Retry; fresh output should return. Reopening is also the recovery action for a renderer process failure in this foundation.

## 6. Record performance

Use the same PC and a consistent CPU accounting method for all measurements. Record the observation interval, Windows display scale, scene and whether a receiver is active. Measure Callayers.exe plus its WebView2 process tree; record Brave separately because the DirectShow source runs in the consumer process. Avoid counting unrelated WebView2 processes as Callayers.

| Scenario | CPU, method and interval | Private memory | User result |
| --- | --- | --- | --- |
| Preview; all cards hidden | Pending | Pending | Pending |
| Preview; animation, timer and annotation | Pending | Pending | Pending |
| Minimized; Meet receiving | Pending | Pending | Pending |
| Minimized; no camera consumer | Pending | Pending | Pending |
| Fully quit | Pending | Pending | Pending |

Task Manager's default Memory column is not private bytes. Have the assistant guide selection of a suitable process memory counter before comparing private-memory numbers. For quit, confirm the desktop and its renderer children exit; Brave can remain running to supply the source's neutral output. After force-stop, separately note whether any renderer children remain.

Earlier reported values were 11.42% CPU for preview without cards, 11.25% with animation, 5.4% with a consumer and editor hidden, and 304–394 MB retained in tray mode. Their original measurement details are incomplete, so these are directional comparisons, not exact pass/fail limits. No new result has been recorded.

## Result log

| Check | Status | User observation / next action |
| --- | --- | --- |
| Startup and preview | Working, user reported on October 13, 2026 | Following the startup-navigation correction, the user reported “great, working” in response to the request to check webcam and cards. Earlier retries failed because our navigation blocker cancelled the bundled overlay load. |
| Transparency and live animation/timer | Specific checks pending | Preview success is recorded; these details have not been separately reported. Confirm alongside the receiving picture in Meet. |
| Meet discovery and received composition | Pending | |
| Private handles, movement, resize and pen | Pending | |
| Timer/animation restart on show | Pending | |
| Minimized active call | Pending | |
| Minimized release and reconnect | Pending | |
| Quit confirmation and neutral output | Pending | |
| Force-stop, stale bound and restart | Pending | |
| Camera disconnect and Retry | Pending | |
| Performance comparison | Pending | |

Do not expand into sign-in, AI, saving or the full editor until these foundation results have been discussed and failures in the video path addressed.
