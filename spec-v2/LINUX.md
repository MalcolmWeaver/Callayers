# Linux app

The Linux app is the same product as [APP.md](APP.md), on the same backend as [BACKEND.md](BACKEND.md). Cards, annotations, sign-in, allowances, and telemetry follow [AI.md](AI.md) and [DATA.md](DATA.md). Video follows [FRAMES.md](FRAMES.md) except for the differences in this document.

The shell is Qt 6 Widgets. The real webcam and the Callayers camera are both V4L2 devices. The app does not implement a PipeWire node, a portal, or a camera helper process.

## Decisions

| Choice | Decision |
| --- | --- |
| Desktop | Ubuntu 24.04 or 26.04 x64, Wayland or X11, through Qt |
| UI | Qt 6 Widgets, using Ubuntu's Qt 6 packages |
| Cards and annotations | Ultralight CPU, offscreen, bundled in the package |
| Webcam | Open the chosen `/dev/video*` node with V4L2 |
| Virtual camera | `v4l2loopback`, device `/dev/video42`, label `Callayers` |
| Picture on that device | 1920×1080 YUYV at 30 fps, converted from the same composited frame as the preview |
| After Callayers stops writing | The device keeps the last frame. No neutral-frame helper |
| First calling app to prove | Zoom |
| Install | One command from the website, which installs a signed apt repository and the package |
| Backend | The existing service. No Linux-specific API |

## Workspace files

Save the same workspace and cards described in [DATA.md](DATA.md), under the standard Linux user directories:

| What | Where |
| --- | --- |
| Cards and `workspace.json` | `$XDG_DATA_HOME/Callayers`, or `~/.local/share/Callayers` |
| Window and camera preferences | `$XDG_CONFIG_HOME/Callayers`, or `~/.config/Callayers` |
| Sign-in session | The desktop Secret Service, via libsecret |

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

Before it changes the machine, the script requires Ubuntu 24.04 or 26.04 on x64. If Secure Boot is enabled, it stops with one message: the Callayers camera needs the `v4l2loopback` module, and Secure Boot is blocking it. It does not enroll a machine-owner key and it does not leave the app installed without a camera.

On a supported machine it then:

1. Installs the header package for the running kernel, `linux-headers-$(uname -r)`, so the camera module can build. It also installs `linux-headers-generic` when that metapackage exists, so a later generic kernel update can rebuild the module.
2. Installs the Callayers signing key and adds `https://downloads.callayers.com/linux/apt`.
3. Runs `apt install callayers`. That package depends on Ubuntu's `v4l2loopback-dkms` and Qt 6, installs the app and desktop entry, writes the `/dev/video42` module configuration, and loads the module.

A reboot is not part of a successful install. A later logout is required only if the user was just added to the `video` group. Later versions come from `apt upgrade`. There is no separate updater process.

`install.sh --deb callayers_<version>_amd64.deb` installs a locally built package with the same checks and kernel-header step. That is how a build is installed before the public repository is published. `dpkg -i` alone does not install the camera module.

The package installed from the repository is signed by the archive key. The first local `.deb` used before that repository exists may be unsigned, matching the current Windows tester release.

## Build

The Linux app is a separate CMake project. It does not replace the Windows build.

```bash
cmake -S linux -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
linux/packaging/build-deb.sh
```

## Sign-in

Use the existing Google desktop client, loopback listener, and Firebase exchange. Open the system browser, then bring the Qt window forward when the callback succeeds. The completion page remains the fallback when the window manager leaves the browser in front.

## What this does not add

No PipeWire or portal implementation, no camera helper, no second distro package, no Flatpak, and no change to the backend, card format, or Windows app.
