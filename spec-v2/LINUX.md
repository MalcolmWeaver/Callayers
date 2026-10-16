# Linux app

The Linux app is the same product as [APP.md](APP.md), on the same backend as [BACKEND.md](BACKEND.md). Cards, annotations, sign-in, allowances, and telemetry follow [AI.md](AI.md) and [DATA.md](DATA.md). Video follows [FRAMES.md](FRAMES.md) except for the differences in this document.

Parity follows the accepted Windows functionality, interaction, and visual design. Linux-native window borders, menus, and file dialogs are allowed; identical pixels are not required. The public beta targets Ubuntu 22.04, 24.04, and 26.04 x64 on Wayland and X11. Debian 12/13, compatible Ubuntu-based Mint/Pop!_OS releases, traditional Fedora 43/44, and Arch Linux have an explicit experimental installer option; their desktop and camera behavior is not advertised as verified. Public apt-repository and website publication are separate from a locally installable package.

Development checks have measured 30 fps camera capture and working virtual output. Fresh production installation, real sign-in, calling-app behavior, and reboot are release acceptance checks; development results do not establish those.

The shell is Qt 6 Widgets. The real webcam and the Callayers camera are both V4L2 devices. The app does not implement a PipeWire node, a portal, or a camera helper process.

## Decisions

| Choice | Decision |
| --- | --- |
| Desktop | Ubuntu 22.04/24.04/26.04 x64, Wayland or X11, through Qt |
| UI | Qt 6 Widgets, using each target distribution's Qt 6 packages |
| Cards and annotations | Ultralight CPU, offscreen, bundled in the package |
| Webcam | Open the chosen `/dev/video*` node with V4L2 |
| Virtual camera | `v4l2loopback`, device `/dev/video42`, label `Callayers` |
| Picture on that device | 1920×1080 YUYV at 30 fps, converted from the same composited frame as the preview |
| After Callayers stops writing | The device keeps the last frame. No neutral-frame helper |
| First calling app to prove | Zoom |
| Install | One shared installer with signed APT, DNF/RPM, and pacman/Arch repository backends |
| Backend | The existing service. No Linux-specific API |

## Workspace files

Save the same workspace and cards described in [DATA.md](DATA.md), under the standard Linux user directories:

| What | Where |
| --- | --- |
| Cards and `workspace.json` | `$XDG_DATA_HOME/Callayers`, or `~/.local/share/Callayers` |
| Window and camera preferences | `$XDG_CONFIG_HOME/Callayers`, or `~/.config/Callayers` |
| Sign-in session | The desktop Secret Service, via Qt DBus |

Remember the webcam by its stable V4L2 device identity, not by its `/dev/video` number. Signing out keeps the saved cards. Another account on the same Linux user can open them after signing in.

If the Secret Service is unavailable, keep the app locked and say that the session could not be saved. Do not write the session to an ordinary file.

## Video

Enumerate V4L2 capture devices for the camera selector. Skip `/dev/video42`. Open the selected device directly and read frames on a capture thread. A device that is busy uses the existing **Camera in use** explanation. Permission failures use the existing camera-access explanation, with the Linux action of allowing access to `/dev/video*` for this user.

Compose webcam, cards, and annotations exactly as on Windows, including private editing controls that never enter the output. Preview that picture in a Qt widget. Convert each published frame to YUYV and write it to `/dev/video42`. Drop superseded frames. Do not queue them.

Create the output device with one module configuration:

```text
devices=1 video_nr=42 card_label=Callayers exclusive_caps=1
```

`exclusive_caps` makes other programs list it as a webcam. If `/dev/video42` is already taken by something else, stop and tell the user the Callayers camera number is unavailable. Do not search for another number.

Load this device at install and at boot. `exclusive_caps` makes Zoom treat it as a webcam once Callayers is writing; Zoom may not list it before that writer opens. The app is the only writer. While it is writing, Zoom and the preview show the same composition. When the app quits, signs out, crashes, or is killed, it stops writing and the device repeats the last frame for as long as the module keeps that device. Reopening Callayers replaces that frame with a fresh picture. This is the accepted Linux difference from the Windows unavailable-picture rule.

Minimized behavior matches Windows: keep capture and output while a consumer is connected; release the webcam and suspend card work when minimized with no consumer.

## Calling apps

Prove the camera in Zoom's Linux app. Zoom is the first target because it opens a V4L2 device directly. Google Meet in Chrome is expected to see the same device, including when Chrome reaches it through the desktop's camera portal. Meet does not get a second output path. If Zoom requires a format the device does not offer, fix the device; do not add a PipeWire camera to satisfy one app.

Consumer identity stays best-effort telemetry. Report the name of the other process that has `/dev/video42` open. Report `none` when no consumer is connected and `unknown` when the reader cannot be attributed. Identity failure must not stop video.

## Install

The website shows one command, with a button that copies it:

```bash
curl -fsSL https://downloads.callayers.com/linux/install.sh | sudo bash
```

The script is `linux/packaging/install.sh`. It is the only install path. Do not also offer a downloaded `.deb` as a second, less reliable installer.

Before changing the machine, the script checks the exact distribution and x64 architecture. Ubuntu 22.04/24.04/26.04 is the beta target. Debian 12/13, compatible Mint/Pop!_OS, traditional Fedora 43/44, and Arch Linux require `--allow-experimental`. Fedora Atomic/OSTree editions, other Fedora releases, Arch derivatives, other distributions, and ARM remain excluded. Secure Boot must be off; the installer does not enroll a machine-owner key.

The shared installer chooses its native backend from `/etc/os-release`:

- **APT:** install the running kernel's `linux-headers-$(uname -r)`, add the signed Callayers APT repository, and install the `.deb` with the distro's `v4l2loopback-dkms` and Qt 6 dependencies.
- **DNF:** require RPM Fusion Free, matching `kernel-devel-$(uname -r)`, and `akmod-v4l2loopback`. If RPM Fusion is missing, stop before package changes unless `--enable-rpmfusion` was supplied explicitly. That flag enables only RPM Fusion Free, explains the community repository, validates its pinned public-key fingerprint, and verifies the release RPM signature. Build the matching driver through akmods before installing Callayers from the signed RPM repository.
- **pacman:** perform a full system upgrade with the kernel's matching headers and `v4l2loopback-dkms`, then run DKMS for the running kernel. If the upgrade removed matching headers, stop before installing Callayers and ask for reboot/rerun. Register Callayers' package key and require signatures for packages and its repository database. Standard `linux`, `linux-lts`, `linux-zen`, and `linux-hardened` kernel package names are recognized; another kernel must already have matching headers installed.

All formats include the same `/usr/lib/callayers/setup-camera.sh` package hook. It checks ownership of `/dev/video42`, refuses to unload another virtual-camera setup, and loads the Callayers device. Packages include boot-load configuration. A logout may be required after adding the user to the `video` group. Subsequent updates use the native package manager; no app updater process is added.

For local acceptance before publication, use `install.sh --package <file>` with `.deb`, `.rpm`, or `.pkg.tar.zst`; `--deb` remains a compatible alias. Local test mode does not add the Callayers update repository. Experimental and Fedora RPM Fusion consent flags still apply. A direct package-manager install alone is not the public onboarding path because matching headers and driver setup must happen first.

The website reads published capabilities from the signed APT `InRelease` availability metadata. Fedora/Arch commands remain unavailable until `X-Callayers-Native: fedora arch` and the Fedora version marker are published. Fedora's repository opt-in is a separate, unchecked checkbox that adds `--enable-rpmfusion` to the command only when selected.

## Build

The Linux app is a separate CMake project. It does not replace the Windows build.

```bash
python3 linux/packaging/acquire-renderer.py
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
linux/packaging/build-deb.sh
```

For local camera/UI development:

```bash
cmake -S linux -B build/linux-camera -DCMAKE_BUILD_TYPE=Release -DCALLAYERS_DEVELOPMENT=ON
cmake --build build/linux-camera
./build/linux-camera/callayers
```

This build is labelled **Callayers — Development** and requires sign-in by default, including session restoration, AI generation, and File → Sign out. For isolated local tests, `--skip-sign-in` explicitly bypasses authentication and disables AI generation without loading or changing the saved sign-in session. That flag is only honored in development builds. The package script explicitly disables development mode.

The renderer acquisition script downloads Ultralight **2.0.0-beta.2** for Linux x64 and verifies its pinned SHA-256. It needs Python 3, curl, and libarchive. Development builds and the package include the shared HTML overlay, text editor, renderer resources, libraries, and license notices. Generated cards run in the same restricted iframe scene as Windows; the renderer filesystem only exposes its packaged ICU and certificate resources.

For development without opening either camera, use the development build with `--offline-preview`. This mode still uses the local workspace, so imported cards and edits save normally. Sign-in and AI generation remain available; add `--skip-sign-in` only for unauthenticated local tests.

```bash
./build/linux-camera/callayers --offline-preview
ctest --test-dir build/linux-camera -R 'linux_html_renderer|linux_preview_interactions|linux_development_workspace|linux_sign_in_gate' --output-on-failure
```

The baseline `.deb` derives its minimum system-library requirements from the build host. Release builds use Ubuntu 22.04 and Qt 6.2 as the minimum baseline. CI tests the exact package on Ubuntu 24.04/26.04 and Debian 12/13 without rebuilding or running the camera post-install script. Native RPM and Arch archives wrap that same baseline payload; their dependency/runtime checks run on Fedora 43/44 and Arch containers. Newer build hosts can produce newer dependency requirements and must not be used to create the multi-version release candidate. Package extraction and `callayers --self-check` check bundled renderer loading without opening a camera, accessing saved work, or signing in. They do not establish desktop or calling-app acceptance. `CALLAYERS_BUILD_JOBS` controls package-build parallelism and `CALLAYERS_CMAKE` can select a CMake binary.

Workspace and preference saves use atomic replacement. Existing Linux workspaces remain readable and gain card state/revision metadata when saved. A malformed existing workspace is retained rather than overwritten. Local session, content-created, and preview-card telemetry are bounded and best effort; consumer identity remains best effort.

## Sign-in

Use the existing Google desktop client, loopback listener, and Firebase exchange. Open the system browser, then bring the Qt window forward when the callback succeeds. The completion page remains the fallback when the window manager leaves the browser in front.

## What this does not add

No PipeWire or portal implementation, no long-running camera helper, no Flatpak, and no change to the backend, card format, or Windows app.

## Linux beta releases

See [release operations](../release/README.md#linux-beta) for baseline/native artifacts, signing setup, and the single publish-from-main workflow. Linux publication is independent of Windows releases.
