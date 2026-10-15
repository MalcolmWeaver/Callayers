# Linux app

The Linux app is the same product as [APP.md](APP.md), on the same backend as [BACKEND.md](BACKEND.md). Cards, annotations, sign-in, allowances, and telemetry follow [AI.md](AI.md) and [DATA.md](DATA.md). Video follows [FRAMES.md](FRAMES.md) except for the differences in this document.

The shell is Qt 6 Widgets. The real webcam and the Callayers camera are both V4L2 devices. The app does not implement a PipeWire node, a portal, or a camera helper process.

## Decisions

| Choice | Decision |
| --- | --- |
| Desktop | Ubuntu 24.04 x64, Wayland or X11, through Qt |
| UI | Qt 6 Widgets, using Ubuntu's Qt 6 packages |
| Cards and annotations | Ultralight CPU, offscreen, bundled in the package |
| Webcam | Open the chosen `/dev/video*` node with V4L2 |
| Virtual camera | `v4l2loopback`, device `/dev/video42`, label `Callayers` |
| Picture on that device | 1920×1080 YUYV at 30 fps, converted from the same composited frame as the preview |
| After Callayers stops writing | The device keeps the last frame. No neutral-frame helper |
| First calling app to prove | Zoom |
| Install | One unsigned `.deb`, installed with one `apt` command |
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

Load this device at install and at boot so Zoom can list **Callayers** before the app is running. The app is the only writer. While it is writing, Zoom and the preview show the same composition. When the app quits, signs out, crashes, or is killed, it stops writing and the device repeats the last frame. Reopening Callayers replaces that frame with a fresh picture. This is the accepted Linux difference from the Windows unavailable-picture rule.

Minimized behavior matches Windows: keep capture and output while a consumer is connected; release the webcam and suspend card work when minimized with no consumer.

## Calling apps

Prove the camera in Zoom's Linux app. Zoom is the first target because it opens a V4L2 device directly. Google Meet in Chrome is expected to see the same device, including when Chrome reaches it through the desktop's camera portal. Meet does not get a second output path. If Zoom requires a format the device does not offer, fix the device; do not add a PipeWire camera to satisfy one app.

Consumer identity stays best-effort telemetry. Report the name of the other process that has `/dev/video42` open. Report `none` when no consumer is connected and `unknown` when the reader cannot be attributed. Identity failure must not stop video.

## Install

The user installs one package:

```bash
sudo apt install ./callayers_<version>_amd64.deb
```

`apt` installs Callayers, Ultralight, the Qt 6 dependency, `v4l2loopback-dkms`, and matching kernel headers. The package setup writes the module configuration, loads `v4l2loopback`, and installs a desktop entry. A reboot is not part of a successful install. A later logout is required only if the user was just added to the `video` group.

Use `apt install` with the package path. `dpkg -i` alone does not install the camera module.

A newer build is installed with the same command and a newer package. There is no separate updater process. A hosted repository can turn that into one remote command later; it is not required for the first Linux build.

Secure Boot can refuse the unsigned loopback module. Detect that failure during install and stop with one message: the Callayers camera needs the `v4l2loopback` module, and Secure Boot is blocking it. Leave the desktop entry uninstalled in that case so the user does not open an app with a missing camera. Do not attempt to enroll a machine-owner key from the package.

The first tester package is unsigned, matching the current Windows tester release.

## Sign-in

Use the existing Google desktop client, loopback listener, and Firebase exchange. Open the system browser, then bring the Qt window forward when the callback succeeds. The completion page remains the fallback when the window manager leaves the browser in front.

## What this does not add

No PipeWire or portal implementation, no camera helper, no second distro package, no Flatpak, and no change to the backend, card format, or Windows app.
