# WinUI workspace — feature verification

Goal: use the existing video foundation through the dark WinUI workspace, with usable camera help and private editing controls.

Status: closed October 13, 2026 by explicit user verification: “okay we can consider this whole task verified i read the spec and it works.” Current tested executable SHA256 `54EB0E3B14251D1B920C9D055E10D0285A180D26AA89D9CE16EEF2FDE63E46C4`. Startup log reaches controls-ready. Initial launch failures were addressed by delay-loading Microsoft.UI.Windowing.Core.dll and moving theme configuration after XAML initialization. User accepted the overall task after their own spec review. No separate numeric resource recheck was supplied; do not claim new measurements. Card bounds/content resize behavior is a separate design follow-up recorded in the delivery plan. Process clarity feedback remains unreported. Preserve the video pipeline and 720p/30 output. Camera persistence, saved/imported cards, AI/auth and complete annotation tools belong to later improvements.

- [x] **1. Workspace and camera — accepted by overall user report**
  - [x] Release x64 build prepared with `.\scripts\build.ps1 -WorkspaceBuild`; compilation/linking succeeded without warnings. Executable SHA256 `168864CE59C9CBD9DFD58E87A4B7E619043CF6E62C9D2F9B9D88C5E362F0DCAC`. Four XAML fragments parsed structurally with no duplicate names, build/measurement scripts parsed, and diff whitespace checks passed. Windows App Runtime 1.6 installation confirmed read-only. Existing foundation executable retains hash `51529BD073156504D575EEB1FDFDB03A8B513C92229920BC9DD0280A341E54B1`.
  - [x] User confirms the corrected build opens and the UI is visible: “okay it works, ui is not the worst.” Launch fixed; this is not blanket acceptance of the design or every control.
  - [ ] Confirm intended webcam/cards appear; camera controls and toolbar work through the new UI.
  - [ ] Resize window; controls stay usable and preview preserves aspect ratio. Camera disconnect/Retry provides understandable help and recovery.
- [x] **2. Editing and presenting — accepted by overall user report**
  - [x] Click Name card text: user confirmed selection, unchanged checkbox and private preview handles on the visible card.
  - [ ] Select a card by name independently of its visibility checkbox; search and Hide all work. Move/resize a shown card and draw/clear annotations.
  - [ ] Meet receives live webcam/cards/timer and edits, with no selection handles or application controls in outgoing video.
  - [ ] Minimize during the call; video and timer continue. Release Meet; unused camera capture stops. Quit warns when output is active and stops it.
- [x] **3. Usability and closure — accepted by user; resource recheck not performed**
  - [ ] Additional numeric resource sample was not performed/reported; the user explicitly closed verification based on their experience. No new CPU/private-memory pass is claimed.
  - [x] User accepts usability/overall task. No explicit process-feedback answer yet.
  - [x] Final outcome/tested build and scoped resize follow-up recorded for Git; remove this temporary file in the subsequent closure commit.

Walkthrough ends after these affected behaviors and the resource comparison. Guide one concrete action at a time and wait for the user's observations. Do not repeat the complete foundation suite or reopen the deferred performance architecture investigation.

## First action: open the workspace

Second user retry still did not open. Windows Application Error events record an access violation (`0xc0000005`) in Microsoft.UI.Xaml.dll; a zero-byte resources.log establishes the app reached logger initialization but died before its first flush. The runtime sequence changed Application.RequestedTheme before WindowsXamlManager initialization. Move theme configuration after XAML initialization and add immediate startup checkpoints in `diagnostics/workspace-startup.log`. Replacement compiled successfully, SHA256 `54EB0E3B14251D1B920C9D055E10D0285A180D26AA89D9CE16EEF2FDE63E46C4`; user retry pending. This is a concrete initialization correction, not yet a confirmed runtime fix.

Close any other Callayers instance. Launch this exact build from PowerShell:

```powershell
cd C:\Users\malco\callayers
& .\build\workspace-x64\Release\Callayers.exe
```

Expected: dark header with logo, File, Annotate, webcam dropdown and short status; left Cards sidebar; large preview; compact Select/Pen/Clear toolbar. If no webcam is selected, the preview offers camera selection and Retry. Choose the intended webcam and expect live webcam and the three sample cards. AI and Import image remain disabled for later milestones; current edits are temporary. Report the layout/picture observation before proceeding. Startup diagnostics are in `build/workspace-x64/Release/diagnostics/resources.log`; renderer details are beside it in `renderer.log`.

Later resource command, after the editing/Meet checks and with the original webcam and same sample scene:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\measure-resources.ps1 -Scenario MinimizedMeet -Seconds 30 -WorkspaceBuild
```
