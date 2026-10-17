# Callayers

Windows app that combines webcam video with interactive cards and annotations, then exposes the result as a virtual camera for video calls.

## Code overview

Video flow: **webcam → Media Foundation capture → CPU composition → native preview + DirectShow virtual camera**. Ultralight renders cards and annotations; editing controls stay private to the app.

Cards and annotations render at 1920 × 1080 for the 1080p/30 virtual camera output. Layouts retain their 1280 × 720 logical coordinates and apparent size; webcam capture still prefers native 720p/30.

| Location | Purpose |
| --- | --- |
| `native/app.cpp`, `native/workspace.*` | Application coordination and WinUI 3 interface |
| `native/capture.*`, `native/overlay_ultralight.*` | Webcam capture and HTML rendering |
| `native/frame_bridge.h`, `native/camera_filter.cpp` | Shared video frames and virtual camera |
| Other `native/` modules | Authentication, saved cards, AI requests, annotations and private text editing |
| `web/` | Card overlay and text-editor HTML/CSS |
| `backend-v2/` | Node.js API for authentication, AI creation/editing and telemetry |
| `scripts/`, `tests/`, `renderer-test/` | Build/deployment helpers and automated checks |
| `release/`, `website/` | Installer/updates and public website |
| `spec-v2/` | Specifications and delivery plan |

## Windows development

Use Windows 10/11 x64, 64-bit PowerShell, Visual Studio Build Tools with **Desktop development with C++** and the Windows SDK, CMake 3.24+, and Windows App Runtime 1.6. The build script downloads the pinned SDK dependencies, including Ultralight.

From the repository root, build all app features into the stable `build/dev-x64` folder:

```powershell
.\scripts\build.ps1 -DevelopmentBuild -ServiceUrl 'https://callayers-v2-api-3fj7uh2a7a-uc.a.run.app'
```

Development builds run directly; installer and automatic-update tooling are covered in the release guide. `-ServiceUrl` writes the backend address beside the executable; replace it when using another deployment. Sign-in requires backend access. See [backend setup](backend-v2/README.md) for API configuration and development (Node.js 22).

Close Callayers and fully exit calling apps before registering this build's camera. Run once after building, and again if the build output path changes:

```powershell
$camera = (Resolve-Path '.\build\dev-x64\Release\CallayersCamera.dll').Path
$registration = Start-Process "$env:WINDIR\System32\regsvr32.exe" -ArgumentList @('/s', ('"' + $camera + '"')) -Wait -PassThru -WindowStyle Hidden
if ($registration.ExitCode -ne 0) { throw 'Camera registration failed.' }
```

Launch directly, sign in, select a webcam, then select **Callayers** as the camera in your calling app:

```powershell
& .\build\dev-x64\Release\Callayers.exe
```

Run automated checks:

```powershell
ctest --test-dir build/dev-x64 -C Release --output-on-failure
```

Run one Callayers instance at a time. Close it and any calling app using its camera before rebuilding; a loaded camera DLL can block the build. Keep the registered output folder in place. `Start Callayers.cmd` launches the older default build; use the commands above for full features.

## Mac development

Use full Xcode and an Apple Development certificate for the configured team. In Xcode, select the team under Settings → Accounts and create the certificate through Manage Certificates if needed. Keep the same team and bundle identifiers across local rebuilds so Keychain and camera authentication can recognize Callayers.

Close Callayers and calling apps using its camera, then build from the repository root:

```sh
bash scripts/build-macos.sh --team TEAM_ID
```

Replace `TEAM_ID` with the configured team (`U42WUQ78VP` on the initial Mac). If full Xcode is not the selected command-line developer directory, prefix the command with `DEVELOPER_DIR=/path/to/Xcode.app/Contents/Developer`; the initial Mac uses `/Users/malcolm/Downloads/Xcode.app/Contents/Developer`.

The script signs the app and embedded camera extension with Apple Development, verifies signatures, and runs native checks on a matching host. The signed app is written to `build/macos-camera-<arch>/Build/Products/Debug/Callayers.app`; use the exact path printed when the build finishes. `--arch arm64` or `--arch x86_64` selects a specific processor family. Cross-compiling does not verify runtime behavior on that processor.

For normal interactive and virtual-camera verification, install that signed bundle as `/Applications/Callayers.app` and launch that copy:

```sh
open /Applications/Callayers.app
```

Use one Callayers instance at a time. Open Camera setup when installing or deliberately updating the camera extension; rebuilding the host alone does not prove a new extension is active. Verify the changed behavior using the short checks supplied in chat under [AGENTS.md](AGENTS.md).

Use team-signed development builds whenever the app will access the real sign-in session, webcam, or virtual camera. Unsigned or ad-hoc builds are for isolated automated fixtures and static asset inspection. Local development signing does not require notarization on each iteration. Public downloads use the separate Developer ID signing, notarization, and publication workflow in [release operations](release/README.md#mac-build-and-release-preparation).

More: [release and website setup](release/README.md) · [delivery plan](spec-v2/README.md) · [feature verification](AGENTS.md)
