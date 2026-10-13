# The app

Callayers lets people put cards and annotations over their webcam. Keep the [reference screenshot](references/ui.png)'s dark style, left-hand card library, and large preview.

## The user journey

1. **Sign in with Google.** Remember the session. No editing or live camera output before sign-in.
2. **Choose a webcam once.** Require a working picture before opening the workspace. On later launches, automatically open the remembered camera. If it fails, return to camera selection. Never silently switch cameras.
3. **Create a card or import an image.** It appears in the library and saves automatically. Check its box to show it on camera; select its name to edit it.
4. **Use in a call.** Tell the user to choose **Callayers** in their calling app. Confirm when video is reaching that app.

There is no setup wizard or saved setup progress. If the camera disconnects later, preserve the user's work and offer reconnection.

AI prompts can include a website address, with or without `https://`, to use its text and basic branding. See [AI.md](AI.md).

## Sign in

1. **In the app:** Callayers logo and one **Sign in with Google** button.
2. **In the default browser:** Google's account chooser, branded for **Callayers**.
3. **On Google:** confirm sign-in if asked. Google controls this screen and may skip it for returning users.
4. **Back to Callayers:** finish authentication, bring the app forward, and continue to webcam selection or the remembered webcam.

The browser ends on a simple Callayers page: **Signed in. You can return to Callayers and close this tab.** This is the fallback if Windows does not bring the app forward.

No second login page, copied codes, email/Twitch options, or automatic tab-closing requirement. Remember the session. If cancelled or unsuccessful, keep the app locked and offer Retry.

## Camera selection and help

Before selection, use the preview space for **Choose your webcam**, the explanation **Your camera and cards will appear here**, and a camera dropdown. Replace this with live video once the camera works. Keep the header dropdown available for changing cameras later.

When something goes wrong, explain the problem and the next action in that same space:

| Problem | Message and action |
| --- | --- |
| Remembered camera missing | **Your saved webcam isn't connected.** Reconnect it and Retry, or choose another camera. If none are listed, ask the user to connect one. |
| Permission denied | **Windows is blocking camera access.** Open Windows camera privacy settings, allow camera access for desktop apps, then Retry. |
| Camera reports that it is in use | **Your webcam is being used by another app.** Close that app's camera preview or stop its video, then Retry. Choose another camera if needed. |
| Cannot open or read the camera; cause unknown | **We couldn't get a picture from this webcam.** Close other apps that might be using it, then Retry. If that doesn't help, reconnect it or choose another camera. |

Only name a cause when Windows provides enough information. A camera light turning off can help indicate release, but is not a reliable requirement or diagnosis. Keep the remembered choice visible and let the user retry; do not ask them to guess technical error codes.

After the camera works, the empty sidebar says **Create your first card**. Keep **In your calling app, choose Callayers** beside Use in a call. Show each instruction where it is useful rather than putting a three-step checklist over the preview.

## The workspace

- **Header:** logo, File, Annotate, camera selector, short status.
- **Left sidebar:** Create with AI, Import image, Hide all, Search cards, and saved card rows with visibility checkboxes. Show recently created or edited cards first. Card menus offer rename, change stacking order, and delete; stacking order is separate from library sorting.
- **Card selection:** show its content and editing controls in the sidebar without changing whether it is broadcast. Checkboxes control visibility; reopening restores the previous visibility automatically. Search filters by name.
- **Preview:** move and resize shown cards. Resize the whole card proportionally: text, graphics, borders and spacing scale together from the card's content dimensions. Preserve aspect ratio; resizing does not restart timers/animations or reflow the HTML document. Editing handles never appear in the outgoing video.
- **Annotations:** Select, pen, shapes, text, color, size, Eraser, and Clear annotations. Follow the [annotation-bar reference](references/annotation-bar.png), replacing Undo/Redo with Eraser. Use simple color swatches; the full spectrum/hex picker is optional.
- **File:** account/sign out and quit.

Remove the deck title and Save button. Never show tokens, generation costs, model names, FPS, resolution, service-startup controls, or raw technical errors.

## Saving cards

**Your cards are saved automatically on this computer.**

| Try this | Expected result |
| --- | --- |
| Create an AI card or import an image | Add it to the sidebar and save it. |
| Edit or rename a card | Update and save the same card. |
| Reopen Callayers | After sign-in and successful webcam connection, restore the workspace and automatically show previously visible cards and annotations in preview/output. No extra confirmation. |
| Delete a card | Confirm, then remove it from the library and camera output. |

AI cards get descriptive names; imported images use their filename. Users can rename either. If saving fails, keep the work available and show **Couldn't save this card. Retry.** Quitting may lose unsaved changes; no recovery flow for this MVP.

## Everyday behavior

- Save on-camera placement and annotations automatically. Showing or hiding cards keeps annotations.
- Hidden cards stop running. Showing them again or reopening the app restarts timers from their configured initial value and animations from the beginning; progress is not saved. Minimizing during a call keeps timers and animations running. When minimized without a video consumer, card work stops; restoring the window or reconnecting a consumer restarts timers and animations.
- Select annotations to move or resize them. Double-click text to edit it; clicking away finishes editing. Simple color swatches change the selected annotation or the next annotation's color.
- Eraser removes a whole stroke, shape, or text annotation. Highlight the object under the pointer before erasing; never erase card content or webcam pixels.
- Clear annotations asks for confirmation, then removes all annotations. Erasing and clearing save immediately and cannot be undone.
- No general Undo/Redo or annotation history. Normal text-field editing shortcuts still work.
- After a successful AI revision, keep one previous version of that card, even after restarting the app. Each new successful revision replaces that backup. Failure or cancellation changes neither version.
- Offer **Restore previous version** in the card menu when a backup exists. Restore its content and name, keeping its current placement and visibility. This uses up the backup: no redo. No AI request or allowance is needed.
- Minimize keeps a call running. Close quits, warning if video is in use. No tray mode or automatic startup.
- Sign-out stops video and locks the workspace, but keeps saved cards.
- If the user's daily AI allowance runs out, explain when it resets. Only AI creation and revision pause; image import, annotations, saved cards, preview, and virtual-camera output still work.

## Out of scope

Saved-card import/export, migration from the old app, choosing save locations, Save/Save as, recovery screens, cloud card sync, multiple scenes, recording, audio, zoom, open-ended web search, site crawling, billing, admin screens, background replacement, and interactive games/forms inside cards. Image import and simple card animation stay. No special timer restoration or saved JavaScript execution state.

## Implementation details

Remember the camera's device identity, not its dropdown position. Store cards under `%LOCALAPPDATA%\Callayers\Cards`, with unique IDs as filenames and human-readable names in the library. Users do not need to browse this folder; renaming and duplicate display names must not break saves.

Save the current card and its one backup safely across interrupted writes. A late AI response must not overwrite a deleted or subsequently edited card. Do not maintain other history or undo stacks. Installation handles camera registration and runtime prerequisites; users do not run developer scripts. See [DATA.md](DATA.md) for storage and [FRAMES.md](FRAMES.md) for camera behavior.
