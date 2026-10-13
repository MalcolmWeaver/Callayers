# Video foundation — feature verification

Goal: show webcam, cards, and annotations in Meet reliably, at a reasonable resource cost.

Progress: **Walkthrough and measurements complete. Picture and lifecycle confirmed; active resource cost still needs an acceptance decision. Process feedback is next.**
Results below are the user's observations and user-run measurements from this walkthrough, not inherited results or agent-run tests. Preview report records revision `da5c1d9`, executable SHA256 `51529BD073156504D575EEB1FDFDB03A8B513C92229920BC9DD0280A341E54B1`.

- [x] **1. The picture works**
  - [x] Live webcam, cards, and running timer appear in Callayers and Meet.
  - [x] Moving/resizing a card and drawing an annotation updates Meet; selection outlines and resize handles stay private.
- [x] **2. The app behaves properly**
  - [x] Minimize during a call: webcam stays live and the timer continues without restarting.
  - [x] Release Meet's camera while Callayers stays minimized: webcam light turns off immediately. Resource use is checked below.
  - [x] Reconnect Meet while Callayers stays minimized: video returns and the timer restarts.
  - [x] Quit during output: user confirmed the expected quit/unavailable-output behavior; Meet does not retain the last webcam picture.
  - [x] Reopen, unplug/reconnect the webcam, and Retry: unavailable output on disconnect; webcam and cards return after Retry.
- [ ] **3. Resource usage is reasonable**
  - [x] Preview with cards, no Meet consumer: Callayers including renderers averaged **12.25% CPU, 205.79 MiB private memory** over 30 seconds. Recorded, not yet judged acceptable. Report: `20261013-134432-PreviewCards-625f52-summary.json`. Brave process churn produced warnings; Callayers and its six renderer identities remained stable in the saved samples.
  - [x] Meet receiving, Callayers minimized: measured **10.92% CPU / 211.38 MiB** for Callayers, **5.50% CPU / 3962.77 MiB** for all Brave processes; **16.42% combined CPU**, no sampling warnings. Direct-webcam Meet measured **3.45% CPU / 3900.27 MiB** for Brave, no warnings: roughly **13 percentage points extra CPU** with Callayers, conditional on comparable call/browser conditions. Reports: `20261013-134615-MinimizedMeet-56a782-summary.json`, `20261013-134814-RawCameraMeet-22b70a-summary.json`.
  - [x] Minimized with no consumer: **0% measured CPU, 154.35 MiB private memory**, seven retained desktop/renderer processes, no warnings over 31 seconds. Idle suspension works; memory is retained until quit. Report: `20261013-135051-MinimizedIdle-426aca-summary.json`.
  - [x] Quit: direct-webcam sample after quitting recorded zero tracked Callayers/renderer processes, CPU and private memory. This also covers quit cleanup; no separate duplicate measurement needed.
  - [ ] Review the numbers together and decide whether the resource cost is acceptable or needs a specific fix.

## Commands for this workspace

Launch from PowerShell (no need to relaunch an already running app):

```powershell
cd C:\Users\malco\callayers
& ".\Start Callayers.cmd"
```

The measurement helper has been restored from repository history. For preview, close Meet's camera use, leave Callayers visible with all three cards and one annotation, and place PowerShell beside it. Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\measure-resources.ps1 -Scenario PreviewCards
```

For minimized-call measurement, let Meet receive Callayers with the same scene, minimize Callayers, let the call settle, then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\measure-resources.ps1 -Scenario MinimizedMeet
```

This gives 5 seconds to settle and samples for 30 seconds. Share the printed summary and any warnings. Reports are saved under `.local/verification`. CallayersTotal includes its renderer processes; Brave covers all Brave processes. CPU uses the whole machine as 100%; memory is private MiB. Later scenarios will use this same helper, one at a time.

For the direct-webcam comparison, quit Callayers and choose the same physical webcam in the same Meet call. Keep participants, effects, video settings and other browser activity similar, let the call settle, then run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\measure-resources.ps1 -Scenario RawCameraMeet
```

## Finish by reviewing the approach

Idle measurement command (completed): close Meet's camera use, reopen Callayers, confirm its preview, then minimize it with no consumer. Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\measure-resources.ps1 -Scenario MinimizedIdle`.

Assessment: picture, lifecycle, idle suspension and quit cleanup met the observed expectations. Active output costs about 13 extra whole-machine CPU percentage points versus direct webcam in these samples. Recommend treating reduction of that cost as a focused improvement; performance acceptance has not been assumed. No further walkthrough checks are currently planned.

- [x] Review the approach with the user: user confirmed “this was good.” Incorporate visible progress and exact build commands.
- [x] Agree on the resulting feature-building and verification guidance and save it in the repository's AGENTS.md, including model recommendations.

Keep this checklist as the source of progress. Walk through one unchecked item at a time and record the user's observation. Do not add speculative checks; explain and agree on any necessary scope change. Fix failures before proceeding.
