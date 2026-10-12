# Callayers telemetry and product questions

Last reviewed: October 12, 2026.

Telemetry should tell us whether people get Callayers working, use it in another app, return to it, and get useful results from AI. It should also help us find failures and control AI spending without collecting presentation contents.

This is the product measurement reference. [Cloud rollout](cloud-rollout.md) covers configuration and manual verification. **Implemented** below means present in the current source; it does not establish that a build is deployed or that an event has been verified in production. Proposed additions are not implemented commitments.

## Questions we want to answer

| Question | What we track now | What this can tell us and what is missing |
| --- | --- | --- |
| Who opens Callayers, and do they return? | Account ID, installation ID, process session ID, process start/end, heartbeats | Distinct signed-in accounts and observed sessions. Background uptime is not active use. People who never sign in are missing. |
| Do people get the virtual camera working? | `setup_completed`, `vcam_state_changed` | First detected consumer per installation/service and later consuming/idle transitions. This does not prove a successful meeting or visible frames. Camera consumption still needs production verification. |
| Is Callayers being used for output or just left open? | Camera transitions; heartbeat consumer count and preview visibility | Approximate periods with a detected consumer, separate from preview visibility and process lifetime. It does not identify the consuming app or human audience. |
| Are cards used in presentations? | `card_stage_changed`, `card_output_changed`, heartbeat card count | Cards selected into the scene, and selected cards while a consumer exists. It does not measure clicks inside cards, animation playback, or attention. |
| Are annotations useful? | Heartbeat annotation count in current source | Whether text/ink elements exist at a snapshot. Creation, editing, deletion, and brief use between snapshots are not tracked. |
| Do people try AI, and does it work? | `card_generation` with operation, state, request ID | Create/revise attempts and observed desktop outcomes. Failures lack a reason in this PostHog event. Success does not prove the result was kept or presented. |
| How many tokens and how much allowance does each account use? | Backend AI request ledger and account/month/global spending totals | Provider-reported usage when available, estimated cost, and conservative holds when the provider outcome is unknown. These figures are not currently sent to PostHog. |
| Where do people encounter failures, and do they recover? | `failure`, `recovery`; AI ledger outcome codes | Instrumented component failures and recovery, grouped by code/version. Not comprehensive crash reporting, performance tracing, or a copy of every screen message. |
| Do trials convert to paid access? | Backend trial dates, approval and paid flags, admin audit | Current entitlement and manual changes. No dedicated conversion event, checkout, revenue measurement, or Stripe integration. |
| How many people install, fail setup, or abandon sign-in? | Installation ID attached to authenticated events | Observed installations only. No installer/download funnel, uninstall tracking, or complete sign-in abandonment funnel. |

Telemetry can show behavior and friction. Interviews are still needed to understand why people use a feature, whether results are useful, and why they stop returning.

## Where information lives

| Store | Purpose | Information |
| --- | --- | --- |
| PostHog | Product behavior and selected operational signals | Explicit content-free events, account/install/session identifiers, timestamps, and event-specific properties below |
| Firebase Auth and Firestore accounts | Identity and access | Google-backed account identity, email, approval/paid/disabled state, trial dates |
| Firestore AI request ledger | Allowance enforcement and admin investigation | Request identity, model, operation, status, usage, estimated cost, timing, pricing version, and outcome code |
| Firestore temporary results | Recover a completed AI response after a connection interruption | Generated provider response, including card HTML; expires after 24 hours, with physical deletion following database expiry processing |
| Cloud logs | Service diagnosis | Selected error codes and request identifiers; application logging deliberately omits bodies, credentials, prompts, and email addresses |

BigQuery is not part of the current telemetry implementation. PostHog is not the authoritative spending ledger.

## Event identity and common fields

The desktop sends an event UUID, UTC observation timestamp, event name, process `session_id`, `install_id`, `app_version`, and allowlisted properties. The backend validates the batch and derives the account identity from its authenticated desktop session.

PostHog receives the event UUID as `uuid`, the Firebase UID as `distinct_id`, the process session as `$session_id`, plus `install_id`, `app_version`, and `source=desktop`. The collector sets `$geoip_disable=true` and `$process_person_profile=false`.

- Account IDs are opaque but linkable to email through our account records; they are not anonymous identities.
- PostHog does not receive Google email, name, or profile photo through this collector.
- An installation ID is a locally persisted identifier scoped to the service origin, not a hardware identity or proof of installation.
- A session ID represents a Callayers process run, not a meeting or a Google login session.
- The current event version string is hardcoded to `0.5.0`. It cannot by itself distinguish recent development builds or prove which heartbeat schedule was deployed.

## Implemented event catalog

The common fields above apply to every row. Names and values here describe the current emitter; the collector's allowlist is in [telemetry.ts](../backend/src/telemetry.ts).

| Event | When emitted | Properties | Interpretation |
| --- | --- | --- | --- |
| `process_state_changed` | Cloud tracking starts; normal process shutdown | `state`: `started`, `ended` | Closing the preview to the tray is not a process exit. End delivery is best effort. |
| `preview_state_changed` | Initial observation and visibility changes | `state`: `visible`, `hidden` | Local preview visibility; independent of virtual-camera consumption. |
| `vcam_state_changed` | Initial observation; transition between zero and at least one detected consumer | `state`: `idle`, `consuming`; `consumer_count` | `idle` means zero detected virtual-camera consumers, not that the physical webcam is off. Changes from one to two consumers do not emit a state transition. |
| `setup_completed` | First observed virtual-camera consumer for this installation/service | None | Local persisted milestone. Not emitted anew for every account or every meeting. |
| `session_heartbeat` | Process minute 1, then minutes 6, 11, 16, etc. | `uptime_ms`, `preview_visible`, `consumer_count`, `visible_card_count`, `visible_annotation_count` | Periodic state snapshot. Older builds emitted every minute without these snapshot fields. Missing fields mean unknown, not zero. |
| `card_stage_changed` | Card enters or leaves the selected scene | `state`: `shown`, `removed`; `card_id` | Scene membership, even when the preview is hidden. Not card creation or a manual edit event. |
| `card_output_changed` | Card enters or leaves the selected scene while consuming; consumption starts/stops with cards selected | `state`: `shown`, `removed`; `card_id` | Intended output membership, not proof that another app displayed a frame. |
| `card_generation` | Desktop observes a generation lifecycle change | `operation`: `create`, `revise`; `state`: `started`, `succeeded`, `failed`, `canceled`, `discarded`; `request_id` when available | A request may emit `succeeded` followed by `discarded` when its target changed. Cancellation does not prove provider cancellation or zero spend. |
| `failure` | Instrumented component reports a changed failing condition | `component`, `code`, `native_code` | Stable category and native error number; repeated identical conditions are suppressed. |
| `recovery` | A previously failing component reports success | `component`, `code`, `native_code` | Requires an observed preceding failure in process state. It is not a general sign-in-success or feature-usage event. |

Heartbeat card counts mean cards selected into the scene. Annotation counts are scene text elements plus ink strokes/shapes; images, card internals, and the transient laser trail are excluded. A nonzero annotation count does not establish when it was edited or whether a viewer saw it. After sleep or a delayed UI tick, the scheduler emits one current snapshot and skips missed slots.

## AI usage and outcomes

PostHog answers questions about attempts and desktop outcomes. Join related lifecycle events by account and `request_id`; do not count event rows as separate generations. The backend ledger uses account plus request ID for the authoritative request record.

For admitted provider requests, the ledger records model, create/revise operation, creation/completion time, latency, pricing version and rates, reserved allowance, estimated cost, status, outcome code, and whether the result is uncertain. Provider usage includes input/output tokens and cached/reasoning token details when supplied. Cached and reasoning counts are details of input/output usage, not extra totals to add again.

An uncertain provider outcome may have no usable token counts. Reserved allowance is held conservatively; that amount must not be presented as confirmed provider billing. Locally canceled work can still finish and cost money.

Requests rejected before admission, including AI disabled, missing configuration, account approval, or allowance failures, may have desktop events but no AI ledger entry. PostHog currently receives no token totals, costs, model, or failure-reason property on `card_generation`.

Prompt text is not intentionally logged in PostHog or the request ledger. The ledger stores a request fingerprint for idempotency, not a readable prompt. AI requests still send prompts and selected context to the service and provider. Temporary generated responses are a separate content-bearing store, not analytics.

## Privacy and delivery boundaries

Analytics contains no annotation text or stroke coordinates, card HTML or field contents, prompts, image files, webcam frames, screen recordings, or full on-screen error messages. The app supplies explicit events; it does not implement PostHog replay or click autocapture.

Events require an authenticated desktop session to upload. Pre-sign-in launch events may be queued and attributed to the first signed-in account. People who never sign in are therefore absent from this dataset. Pending queues are bounded to 1,000 events, encrypted for the Windows user, and scoped to the service origin. Account switching removes other-account events; sign-out clears pending events.

Normal batching occurs about every 30 seconds, followed by ingestion delay. Retries retain event UUIDs for deduplication. The collector rejects unknown fields, limits batch sizes, and discards events outside its seven-day timestamp window. Abrupt shutdown, queue overflow, disk failure, or continued delivery failure can lose events. Deploy collectors before clients that introduce new fields; old collectors can reject those batches.

An absent end event does not prove a crash. Do not extend a consuming state indefinitely when a process disappears. Use the last observation as a bound and label incomplete intervals. Account/admin records and request ledgers currently need operator-managed deletion; PostHog retention must be chosen in project configuration. There is no implemented self-service account deletion flow.

## Suggested first analyses

These are working product definitions, not existing dashboards or approved business metrics. Exclude staff/test accounts and state the date range and denominator when reporting them.

1. **Observed setup activation:** distinct accounts with `setup_completed` among accounts observed starting Callayers in the cohort. Label the installation-scoped milestone and missing unsigned-in users; validate camera tracking before relying on this result.
2. **Return to camera use:** distinct accounts with a detected consumer on separate days/weeks. Report app launches separately so background processes are not mistaken for presentation use.
3. **Card use with a consumer:** accounts and sessions with `card_output_changed / shown`, after verifying those events. This measures intended output, not viewer engagement.
4. **AI attempts and outcomes:** distinct request IDs by create/revise and lifecycle state. Keep discarded/canceled outcomes visible; do not assume all lifecycle states are mutually exclusive.
5. **AI spending:** backend usage/cost by account and month, separating confirmed usage estimates from uncertain holds. Use this to investigate heavy usage and allowance exhaustion.
6. **Reliability:** affected accounts and occurrences by component, code, and build. Counts are instrumented failures, not a complete crash or failure rate.

## Verification status and gaps

Evidence from the October 12, 2026 read-only PostHog inspection showed process start/end, preview visible/hidden, minute heartbeats from the older behavior, authentication recovery, and two generation attempts ending in failure. It showed only an initial camera `idle` event despite a reported virtual-camera test. These observations are historical evidence, not a fresh check of the current deployment.

| Gap or addition | Status | Next evidence or decision needed |
| --- | --- | --- |
| Camera consumption history | Implemented, not verified in production | Reproduce a known consumer start/stop; reconcile local consumer detection with received events. Do not assume the earlier missing transition proves no camera use. |
| Setup and card stage/output events | Implemented, not confirmed in the inspected sample | Exercise first consumption and card selection/removal with and without a consumer. |
| Five-minute heartbeats and snapshot counts | Present in current source; production receipt not established here | Deploy collector first, then client; confirm timing and fields, including annotations present/absent. |
| Successful AI generation and token accounting | Implemented, not verified in the inspected sample | Enable correctly priced AI for an approved account; reconcile desktop outcome, request ledger, provider usage, and allowance. |
| Annotation actions | Proposed, not implemented | Add `annotation_changed` with action `created`, `edited`, `deleted`, or `cleared`, type text/drawing, and consuming state. Define completed edit/undo/redo behavior; avoid per-keystroke or per-pointer events and collect no content. |
| AI failure reasons in analytics | Proposed, not implemented | Add stable categories so configuration/approval/budget/provider failures can be distinguished without collecting messages or prompts. |
| Manual card edits and interactions | Not tracked directly | Decide which actions answer a product question before instrumenting them. |
| Install and sign-in funnel | Not implemented | Decide whether acquisition/abandonment measurement warrants events before authentication and its privacy implications. |
| Build identity | Current version string is insufficient | Use a release/build identifier before comparing behavior across deployments. |

## Maintaining this reference

For each telemetry change, update the product question, event trigger, properties, privacy boundary, client/server compatibility, and verification status here. Keep proposed events separate from shipped events. Record what was actually observed when closing a gap; a successful build alone does not verify ingestion.

Implementation references: [desktop collection](../native/cloud_client.cpp), [heartbeat schedule](../native/heartbeat.h), [scene observation](../native/app.cpp), [AI lifecycle emission](../native/winui_shell.h), [collector schema](../backend/src/telemetry.ts), and [AI ledger](../backend/src/ai.ts).
