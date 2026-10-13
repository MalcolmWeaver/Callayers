# Active resource usage — focused improvement

Goal: reduce the measured CPU overhead of presenting through Callayers while keeping the picture and call behavior usable. This improvement comes before the WinUI workspace.

Status: **closed October 13, 2026 by usability acceptance**. User confirmed “i think it feels accepteable.” The current video experience and measured resource cost are accepted for this MVP setup. No optimization was implemented; CPU compositor improvements and GPU capture/composition experiments are deferred unless a concrete usability problem justifies them. WinUI workspace is next. Unchecked optimization items below were deliberately not pursued, rather than passed. Previous minimized Meet comparison: Callayers + Brave 16.42% CPU versus 3.45% raw; matched recheck overhead 9.535 percentage points. Use 30-second samples throughout. These results are conditional on comparable settings and browser activity.

Recheck report `20261013-140555-MinimizedMeet-128896-summary.json`: 30.80 seconds, no warnings, same executable SHA256 as the previous baseline. Callayers measured 0.127% CPU / 161.46 MiB private memory; Brave 3.517% CPU / 3064.66 MiB; combined CPU 3.644%. This unexpectedly resembles suspended/idle Callayers; confirm Meet actually received live Callayers video throughout before treating it as an active-output measurement. Scenario labels alone do not establish consumer state.

User confirmed the first sample did not have active Callayers output and reran it. Exclude that sample from the active comparison. Corrected report `20261013-140734-MinimizedMeet-602c8c-summary.json`: 31.06 seconds, no warnings, same executable SHA256. Callayers 9.265% CPU / 211.49 MiB private memory (desktop 6.536%, renderers 2.729%); Brave 3.903% CPU / 3562.37 MiB; combined CPU 13.168%. Matched raw-camera measurement remains pending; do not subtract an older unmatched baseline to claim the new overhead.

The user performs runtime verification. The assistant prepares builds, reads reports, locates expensive work and fixes it. Guide one action at a time and wait for observations. Do not rebuild before confirming the current baseline unless required; record executable hash and source state in the measurement reports.

- [x] **1. Confirm the active-call overhead**
  - [x] Measure minimized Callayers feeding Meet: corrected active-call report recorded above following the user's rerun. Matching conditions for the direct-webcam sample remain required.
  - [x] Quit Callayers and measure direct-webcam Meet under matching conditions. User completed the guided action; report `20261013-140932-RawCameraMeet-c477ff-summary.json`: 30.82 seconds, no warnings, Brave 3.633% CPU / 3604.13 MiB private memory, zero tracked Callayers/renderer processes.
  - [x] Compare combined CPU, private memory and sampling warnings. Active Callayers + Brave 13.168% minus direct-webcam Brave 3.633% = **9.535 percentage points extra CPU**. Combined active private memory 3773.86 MiB versus raw 3604.13 MiB, net 169.73 MiB extra in these samples. The same executable was used; the smaller gap versus the earlier roughly 13-point sample is measurement variation, not an implemented improvement. Both matched reports have no warnings; proceed to diagnosis rather than another duplicate pair.
- [ ] **2. Make a focused reduction**
  - [x] Prepare an isolated diagnostic build recording requested/actual camera modes and five-second stage timings; Release x64 compilation succeeded without warnings on October 13, 2026. Runtime verification pending.
  - [x] User confirmed diagnostic preview works. Initial log confirms original webcam native 1920x1080 at 30 fps, NV12 subtype GUID `{3231564E-0000-0010-8000-00AA00389B71}`; RGB32 reader remains 1920x1080, stride 7680. Steady camera delivery approximately 29.8 fps. Preview observations show camera copy roughly 3–4 ms/frame, composition roughly 6.7–8.4 ms/frame, overlay decoding roughly 3–5 ms/capture, overlay capture roughly 101–107 ms elapsed including asynchronous wait (7–8 captures/sec). These are preview wall timings, not CPU attribution or minimized-call measurements. Active-call diagnostic measurement next.
  - [x] User confirmed diagnostic preview and completed the guided minimized-Meet run. Report `20261013-141703-MinimizedMeet-557a82-summary.json`: 30.13 seconds, no warnings, expected diagnostic executable hash; desktop 6.905% CPU, renderer 2.580%, Callayers total 9.484% / 208.58 MiB private memory, Brave 4.554% / 3806.94 MiB. Combined CPU 14.038%; no new matched raw-camera baseline, so do not calculate updated overhead using an older raw sample. Logs continue to show approximately 30 camera copies/compositions per second, composition roughly 6–9 ms/frame, copy roughly 3–4 ms/frame and overlay capture roughly 7–8/sec with approximately 100 ms async elapsed latency. This supports investigating the camera/conversion/scaling path, but does not establish CPU attribution. Next controlled action: same diagnostic build and scene with the smaller webcam, recording its negotiated mode and stage costs.
  - [ ] If the gap persists, measure stage costs to distinguish capture/conversion, overlay capture/PNG decoding, composition and consumer delivery; investigate the largest evidenced cost first.
  - [ ] Implement a focused change, compile and record the candidate build. Preserve fresh video, transparency and card/annotation behavior; do not silently lower output settings.
- [ ] **3. Confirm the result and finish**
  - [ ] Repeat the minimized-call/direct-webcam comparison under matched conditions; report before/after CPU overhead and private memory.
  - [ ] User confirms live webcam, animation/timer, placement/annotation updates and private editing handles in Meet; minimize keeps the call live.
  - [ ] Recheck minimized idle release/resource use and quit cleanup if affected by the change. Reuse prior evidence for unaffected behavior.
  - [ ] Decide together whether the measured cost and video usability are acceptable; record remaining issues as focused follow-up work.
  - [ ] Ask whether this walkthrough was clear and manageable; record the final outcome and tested build in Git, then remove the temporary checklist when closed.

## First action — current build, minimized Meet

Use the current app with all three cards and one annotation. Let Meet receive Callayers, minimize Callayers, and keep the call/browser workload steady. Run:

```powershell
cd C:\Users\malco\callayers
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\measure-resources.ps1 -Scenario MinimizedMeet -Seconds 30 -Notes 'Resource recheck; three cards and one annotation; record Meet effects/settings and other browser activity'
```

Expected: five seconds settling, then 30 seconds sampling, followed by CPU/private-memory summary and any warnings. This records cost; it does not establish acceptance. Share the summary and observations before moving to the direct-webcam measurement.

The walkthrough ends after a matched before/after comparison, a short check of affected video behavior and an explicit acceptance decision. No full MVP walkthrough or broad device matrix belongs here.

## Diagnostic build handoff

Diagnostic smaller-camera run `20261013-141912-MinimizedMeet-b5eece-summary.json`: 30.41 seconds, no warnings, same diagnostic executable hash. Desktop 4.166% CPU, renderer 3.019%, Callayers total 7.185% / 201.84 MiB, Brave 4.616% / 3824.88 MiB. Log confirms native and RGB32 reader **1280x720 at 30 fps**, NV12 native subtype, stride 5120; usable camera delivery approximately 30 fps. Recent steady stage summaries: camera copy roughly 1.2–1.7 ms/frame, compose roughly 5.2–6.2 ms/frame, overlay capture roughly 10–11/sec with 82–87 ms async elapsed latency. Against the original diagnostic camera run, desktop CPU is lower by 2.739 whole-machine percentage points; renderer CPU is higher by 0.439, consistent with more completed overlay captures. These camera runs establish a useful camera-size association, not isolation of resolution from device/driver effects. Prioritize reducing full-resolution camera copying/scaling and unnecessary compositor work; preserve 720p/30 output and compare the original camera before/after. Do not infer that changing cameras alone fixes the remaining overhead.

Smaller-webcam comparison, same existing executable, reported by the user: `20261013-141319-MinimizedMeet-d2fc85-summary.json` records Callayers 6.527% CPU / 197.34 MiB and Brave 4.224% / 3642.27 MiB, combined CPU 10.751%. `20261013-141413-RawCameraMeet-306b9d-summary.json` records Brave 5.482% / 3694.91 MiB and zero tracked Callayers processes. Matched overhead is **5.269 CPU percentage points**, versus 9.535 for the original webcam pair. Callayers itself uses 2.738 fewer points; renderer CPU also varies (2.160% versus 2.729%), and the raw Brave baseline is higher. Camera choice is a useful diagnostic lead; neither negotiated resolution nor the cause of the difference has yet been established. No optimization has been made.

Build: `build/diagnostics-x64/Release/Callayers.exe`, SHA256 `2C499651330D2CD455D387B980B767A55ACA1F7F96E2CD512FB7045429CD417C`. Rebuild command: `.\scripts\build.ps1 -DiagnosticsBuild`. Existing `build/x64/Release/Callayers.exe` still hashes to `51529BD073156504D575EEB1FDFDB03A8B513C92229920BC9DD0280A341E54B1`; it was not overwritten. User is measuring the smaller webcam with that existing build first. Keep those results separate and note the webcam change.

After that comparison, close the existing app and launch the diagnostic app directly, using the already registered camera bridge:

```powershell
& .\build\diagnostics-x64\Release\Callayers.exe
```

Guide only this launch action first; wait for the user's picture observation. Then guide selecting the original webcam, receiving live Callayers video in Meet and minimizing, followed by:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\measure-resources.ps1 -Scenario MinimizedMeet -Seconds 30 -DiagnosticsBuild
```

Read `build/diagnostics-x64/Release/diagnostics/resources.log` and the measurement report after the user completes the run. The log resets on each app launch. Camera subtype is recorded as a GUID; `camera_copy` rate counts delivered usable frames. Stage durations are wall time, not CPU, and do not account for Media Foundation conversion or consumer-side work. Async overlay capture includes waiting. Do not label the slowest elapsed stage a proven CPU bottleneck without corroborating evidence. No resource optimization or output-resolution change is included in this build.
