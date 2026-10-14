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

Two collections. Daily usage is a number on the account. Requests hold AI deduplication metadata and short-lived website-attribution tickets; generated cards save on the computer.

| Collection | What it holds |
| --- | --- |
| `accounts` | Account access, signup date, and today's AI usage |
| `requests` | AI request owner/status with 24-hour expiry; separately prefixed attribution ticket hashes, owner and two-minute expiry. Attribution tickets are deleted transactionally when redeemed; unredeemed tickets use the existing expiry TTL. No HTML or images. |

No separate usage, results, rate-limit, or telemetry collections. PostHog handles product events; analytics retries reuse the same event ID.

Firebase Authentication manages identities and login sessions. No separate password, session, card, or prompt collections.

The backend returns generated HTML and assets to the desktop without saving them to a database, file store, or logs. It does not retain prompts, webcam frames, or annotations. If the response is lost before the desktop saves it, the result cannot be recovered; the user must explicitly generate again, using another allowance unit.

## Possible follow-up: generation archive

If admin review proves useful, retain AI outputs with their owner and creation time and add a protected preview. Explain retention to users and support deletion. Large assets would need file storage. This would show generated outputs, not later local edits or imported images. Deferred; no archive or admin viewer now.

## Telemetry (MVP)

Usage, creation, and video-state changes go to PostHog. Errors stay in operational logs; AI allowance counts stay on the account.

| Event | What it tells us |
| --- | --- |
| `session_started` | One authenticated session per launch/account sign-in; active and returning accounts |
| `video_state_changed` | Preview visibility, consumer connect/disconnect, fresh output and consuming app |
| `content_created` | Successful saved AI creation/revision, image import or completed saved annotation |
| `card_usage_changed` | Card enters/leaves rendered preview or delivered output, with `applied`, `destination`, `card_id` and `card_type` (`ai` or `image`) |

Each event has a UUID v4 `id`, UUID v4 `sessionId`, UTC ISO `timestamp`, `appVersion`, `event` and an approved `properties` object. The backend derives account identity from verified Firebase credentials. Card IDs are opaque local identifiers; names and filenames are excluded. Session events have no additional properties. Creation has `kind` (`ai_create`, `ai_revision`, `image_import`, `annotation`), and a `card_id` except for annotations. Canceled/discarded AI responses, failed saves, restored files and previous-version restoration do not create actions.

Camera and card transitions carry best-effort timestamps. They support approximate duration analysis; there is no separate duration accumulator, exact call/session accounting or required final event after a crash. Preview and output are distinct destinations. Already-visible cards enter usage at session/output start; hide, delete, output stop and preview minimization end the affected destination. Movement, resize, stacking, rename, interactive controls and repeated frames do not generate usage events. Card application follows metadata attached to rendered/delivered frames, not sidebar selection.

For new text annotations, count creation once when the user finishes editing nonempty text by clicking away. Later text edits, movement, resizing, color changes, erasing, and clearing send no event. There is no separate annotation-edit event. Never send keystrokes or annotation text.

Restoring a previous card version sends no creation event and uses no AI allowance.

**Video state:** include `video_state_changed` with `preview_visible`, `consumer_connected`, `output_active`, and `consumer_app`, sent once at session start and whenever any changes. A fast reconnect also closes the previous connection. Preview visibility describes the app's visible preview area; card preview use additionally requires a fresh rendered picture. `consumer_connected` means active camera consumption, even if only neutral video is delivered. Output requires a successfully delivered fresh frame from the current producer session, including camera-only use. Consumer/delivery heartbeats expire after one second. Ending an app/account session ends preview/output use without inventing a consumer disconnect. No frame-by-frame events or extra network heartbeat.

**Consumer app:** required for the MVP. Identify the host application requesting video using a normalized label, such as `zoom`, `brave`, or `chrome`. Use `unknown` when identity cannot be established reliably and `none` when no consumer is active. Browser identity does not identify Google Meet or another website; a camera-settings preview does not establish that a meeting occurred. Send no executable paths, process IDs, window titles or browsing information. Identity failure never interrupts video. See [FRAMES.md](FRAMES.md#consumer-identity-mvp).

No editor activity, error events, prompts, contents, images, email or webcam footage are collected by desktop telemetry. No telemetry-off setting, session replay, autocapture or custom dashboard is added. PostHog receives an account-based `distinct_id` prefixed `callayers:` without name/email profile enrichment; GeoIP is disabled on desktop events. Person processing is enabled to join website acquisition history to authenticated app use.

Website acquisition events are `$pageview`, `get_callayers_clicked`, `installer_download_clicked`, `download_unavailable`, and `desktop_account_connected`. The browser SDK uses first-party cookie/local-storage identity and campaign/referral properties. Explicit download clicks include the release version and button location; failed release lookup uses only a bounded reason. After verified desktop sign-in, a short-lived one-use analytics ticket links the existing browser visitor to the desktop account using PostHog `identify`. The confirmation page removes the ticket fragment before initializing analytics. URLs sent to analytics omit fragments and query parameters other than the five UTM parameters. This measures download intent and authenticated use, not completed installation. A different browser/profile or cleared storage can leave account acquisition unknown.

The desktop buffers at most 128 events in memory, sends batches of at most 32 after two seconds or when full, and makes at most three attempts with two-/ten-second retry delays. Events older than five minutes are dropped before sending. Shutdown makes a bounded final delivery attempt; crashes, long outages and queue pressure can lose events. Retries preserve event ID, event name, original timestamp and account identity for PostHog's eventual deduplication; duplicates can briefly appear before merging. Tokens may renew only for the same account/service. Network/diagnostic writes stay off UI/video threads; telemetry failure never changes authentication, video, saving or AI allowance.

## Configuration and secrets

`/client-config` supplies the public installed-app OAuth configuration and Firebase configuration. The verified Google desktop exchange requires its installed-app client secret; Google treats that value as public client configuration, since installed clients cannot keep a shared secret confidential. It is distinct from server provider credentials. Store the Firebase session securely in Windows; clear it on sign-out. The OAuth callback is local and temporary; after verified sign-in it redirects the browser to a hosted Callayers confirmation page for analytics identity linking. Backend provider secrets never ship in the app.

| Where | Settings |
| --- | --- |
| Desktop | Backend address: `SERVICE_URL` |
| Backend public configuration | `GOOGLE_CLOUD_PROJECT`, `GOOGLE_DESKTOP_CLIENT_ID`, `FIREBASE_WEB_API_KEY` |
| Installed-app configuration | Downloaded desktop OAuth JSON, stored as `callayers-v2-desktop-oauth` in Secret Manager; `/client-config` returns its installed client ID/secret |
| Backend behavior | `AI_MODEL`, optional `AI_REASONING_EFFORT`, `MAX_INPUT_BYTES`, `MAX_OUTPUT_TOKENS`, `USER_DAILY_GENERATIONS`, `POSTHOG_HOST` |
| Secret Manager | `OPENAI_API_KEY`, `POSTHOG_PROJECT_TOKEN` |

One shared setting sets AI generations per user per day. AI creation and revision each use one; repeating the same request ID never counts twice. At the limit, AI pauses until the next UTC day. Image import, annotations, saved cards, preview, and virtual-camera use have no daily allowance. Choose the number before release. Trial expiration and payments are deferred; see [BACKEND.md](BACKEND.md#trial-and-paid-access-later). PostHog settings are only needed when telemetry is implemented.
