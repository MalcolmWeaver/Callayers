# What gets saved

**Cards and your workspace save automatically on this computer. Accounts and AI allowances live in the database.** One workspace, no decks or cloud card library.

## On the computer

| What we save | Includes |
| --- | --- |
| Cards | Names, HTML/CSS/JS, embedded images, prompts, and meaningful interactive state |
| One previous version per card | Name, HTML/CSS/JS, and referenced images from before the latest successful AI revision |
| Workspace | Which cards are showing, each card's position, size and stacking order, selected card, annotations, and unfinished prompt |
| Preferences and sign-in | Webcam choice, window placement, and credentials secured by Windows |

**Simple approach:** keep the card files and one autosaved `workspace.json`. After sign-in and successful webcam connection, restore the workspace, including which cards and annotations appear on camera. Save after edits, not every video frame.

Cards report small, validated state objects through an app-owned interface: entered values, scores, current slide and timer remaining time. Restore these before showing the card. Timers resume paused after unloading; animations restart. Do not serialize the DOM or JavaScript runtime, or grant browser storage/filesystem access. Save state after meaningful changes and before unloading; timer checkpoints are bounded, not per-frame. Revisions/restoration reset interactive state to the replacement content's defaults, avoiding incompatible old values.

The fresh implementation uses `%LOCALAPPDATA%\Callayers\WorkspaceV2\workspace.json`, isolated from old saved work. The annotation milestone saves annotations there now; card placement, visibility and the remaining workspace fields arrive with the saved-card milestone.

Build 2 isolates its trusted fixed card's score, name and remaining timer time in `interactive-card.json` alongside the annotation workspace. A bounded schema and atomic background saves protect that prototype state. This separate fixture file does not implement the saved-card library or placement/visibility restoration.

An imported image is an ordinary card containing that image.

Keep the previous card version locally alongside the current card; it survives restarts. Replace it only after a successful AI revision. Restoring consumes the backup and keeps the card's identity, placement, and visibility. Deleting a card deletes its backup too. No backend storage or new endpoint.

No other undo/redo history. Eraser removes whole annotations; Clear annotations removes all after confirmation. Save these changes without keeping deleted objects.

Cards save automatically on this computer. There is no saved-card import/export or choice of save location. They belong to the Windows user, not the cloud account: signing out keeps them, and another account on the same Windows profile can access them after signing in.

## In the database

Two collections. Daily usage is a number on the account. Requests keep only enough metadata to prevent duplicate AI calls; generated cards save on the computer.

| Collection | What it holds |
| --- | --- |
| `accounts` | Account access, signup date, and today's AI usage |
| `requests` | AI request owner and status, kept for 24 hours to prevent retries from generating or counting twice. No HTML or images. |

No separate usage, results, rate-limit, or telemetry collections. PostHog handles product events; analytics retries reuse the same event ID.

Firebase Authentication manages identities and login sessions. No separate password, session, card, or prompt collections.

The backend returns generated HTML and assets to the desktop without saving them to a database, file store, or logs. It does not retain prompts, webcam frames, or annotations. If the response is lost before the desktop saves it, the result cannot be recovered; the user must explicitly generate again, using another allowance unit.

## Possible follow-up: generation archive

If admin review proves useful, retain AI outputs with their owner and creation time and add a protected preview. Explain retention to users and support deletion. Large assets would need file storage. This would show generated outputs, not later local edits or imported images. Deferred; no archive or admin viewer now.

## Telemetry (implement later)

Usage, creation, and video-state changes go to PostHog. Errors stay in operational logs; AI allowance counts stay on the account.

| Event | What it tells us |
| --- | --- |
| Usage | Which card was shown in preview or delivered through the virtual camera, how many times, and for how long |
| Creation | An AI card was created or revised, an image was imported, or an annotation was completed |

For new text annotations, count creation once when the user finishes editing nonempty text by clicking away. Later text edits, movement, resizing, color changes, erasing, and clearing send no event. There is no separate annotation-edit event. Never send keystrokes or annotation text.

Restoring a previous card version sends no creation event and uses no AI allowance.

**Video state:** include `video_state_changed` with `preview_visible`, `output_active`, and `consumer_app`, sent once at session start and whenever any of these changes. Output means fresh video is reaching at least one consumer, including camera-only use. No frame-by-frame events or new endpoint.

**Consumer app (implement with telemetry):** identify the host application requesting video using a normalized label, such as `zoom`, `brave`, or `chrome`. Use `unknown` when identity cannot be established reliably and `none` when no consumer is active. Browser-based services are reported as the browser: Brave does not establish whether the user is using Google Meet or another website. Active consumption can also be a camera-settings preview; it does not prove a meeting is underway. Send no executable paths, process IDs, window titles, or browsing information. A consumer-app change sends a video-state event even if output remains active. The local identity mechanism is deferred along with telemetry; it is not a prerequisite for the runnable MVP. See [FRAMES.md](FRAMES.md#consumer-identity-implement-with-telemetry).

Include account ID, card ID where applicable, timestamp, and app version. These events also show whether people return. No signup funnel, editor activity, error events, prompts, contents, images, or webcam footage. Count each action once; retries do not create new actions.

## Configuration and secrets

`/client-config` supplies the public installed-app OAuth configuration and Firebase configuration. The verified Google desktop exchange requires its installed-app client secret; Google treats that value as public client configuration, since installed clients cannot keep a shared secret confidential. It is distinct from server provider credentials. Store the Firebase session securely in Windows; clear it on sign-out. The callback page is local and temporary, so no hosted login origin is needed. Backend provider secrets never ship in the app.

| Where | Settings |
| --- | --- |
| Desktop | Backend address: `SERVICE_URL` |
| Backend public configuration | `GOOGLE_CLOUD_PROJECT`, `GOOGLE_DESKTOP_CLIENT_ID`, `FIREBASE_WEB_API_KEY`, `POSTHOG_HOST` |
| Installed-app configuration | Downloaded desktop OAuth JSON, stored as `callayers-v2-desktop-oauth` in Secret Manager; `/client-config` returns its installed client ID/secret |
| Backend behavior | `AI_MODEL`, optional `AI_REASONING_EFFORT`, `MAX_INPUT_BYTES`, `MAX_OUTPUT_TOKENS`, `USER_DAILY_GENERATIONS` |
| Secret Manager | `OPENAI_API_KEY`, `POSTHOG_PROJECT_TOKEN` |

One shared setting sets AI generations per user per day. AI creation and revision each use one; repeating the same request ID never counts twice. At the limit, AI pauses until the next UTC day. Image import, annotations, saved cards, preview, and virtual-camera use have no daily allowance. Choose the number before release. Trial expiration and payments are deferred; see [BACKEND.md](BACKEND.md#trial-and-paid-access-later). PostHog settings are only needed when telemetry is implemented.
