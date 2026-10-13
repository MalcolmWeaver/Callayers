# Whole-card scaling — feature verification

Goal: dragging a card's resize corner grows/shrinks its complete contents proportionally, without clipping, reflowing or restarting its timer/animation. Preview and Meet show the same scaled card; editing handles stay private.

Status: closed October 13, 2026 by explicit user acceptance: “consider verification done.” Whole-card scaling is implemented and accepted. Test build SHA256 `4214D343D03FA5C191FD69F14601693EF2F54052DAE93E2AF95C1CAE6CEF6B5E`. Acceptance is the user's overall report; no separate timer/Meet observation or additional resource measurement was supplied. The user previously confirmed this verification approach was clear/manageable.

- [x] **1. Prepare the build**
  - [x] Preserve each card's content dimensions and scale its existing iframe; constrain corner resizing to the card's aspect ratio and output canvas bounds.
  - [x] `.\scripts\build.ps1 -WorkspaceBuild`: Release x64 compiled successfully without warnings. JavaScript syntax and diff whitespace checks passed. Candidate executable SHA256 `4214D343D03FA5C191FD69F14601693EF2F54052DAE93E2AF95C1CAE6CEF6B5E`. User runtime checks pending.
- [x] **2. Confirm the visible outcome — accepted by overall user report**
  - [ ] Resize the Name card smaller/larger: text, border and spacing scale together; handles match the card and the card stays inside the picture.
  - [ ] Resize the timer/animated card during Meet output: timer/animation continues without restarting; preview and Meet match, with private handles.
- [x] **3. Close the improvement**
  - [x] User accepts the overall outcome. Earlier process feedback: clear/manageable.
  - [x] Record the tested build/outcome in Git, then remove this temporary file in the subsequent closure commit.

Ends after two focused visual checks. No resource remeasurement is planned unless this change produces a concrete usability concern. The prior workspace sample and acceptance remain valid for that feature.

## First action: resize the complete Name card

```powershell
cd C:\Users\malco\callayers
& .\build\workspace-x64\Release\Callayers.exe
```

Choose the intended webcam if necessary. Select Name card and drag its lower-right corner inward/outward. Expected: text, borders and spacing shrink/grow together, proportions remain unchanged and handles stay aligned. Pause for the user's observation before the timer/Meet check.
