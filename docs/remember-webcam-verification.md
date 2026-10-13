# Remember the webcam

```powershell
cd C:\Users\malco\callayers
& .\build\workspace-x64\Release\Callayers.exe
```

- [x] Choose a webcam, wait for a picture, then quit and reopen. The same webcam opens automatically.
- [x] Quit, disconnect that webcam, and reopen with the other webcam available. The saved choice stays visible with Retry; the other camera does not start automatically.
- [x] Reconnect and Retry, then explicitly choose the other camera and reopen. Retry returns to the saved camera; the new working selection is remembered after reopening.
