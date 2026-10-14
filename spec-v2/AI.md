# Creating cards with AI

**The backend generates. The desktop displays and saves.** Build sequence: [AI implementation](AI-IMPLEMENTATION.md); overall order: [master MVP plan](README.md#complete-mvp-sequence).

## Create and revise

- Send the prompt. There is no card-shape selector; infer a suitable layout from the requested content and any sizing instructions in the prompt.
- **Edit** revises the selected AI card or the surrounding HTML of an imported image. Include only the selected card HTML and placeholder image references. The desktop substitutes retained local image data for those references; never send local image bytes or paths. Keep a user-edited library name unless the user renames it locally.
- Return a short name and self-contained HTML/CSS/JS.
- Change what was requested; preserve the rest.
- Keep user-imported images local. Never send webcam frames or other cards.
- After a successful revision, the desktop keeps one previous version. **Restore previous version** works locally without an AI call; see [APP.md](APP.md#everyday-behavior).

## Website reading (deferred beyond MVP)

MVP generation uses the user's prompt and pasted text. It does not retrieve websites or infer page contents from an address; ask for pasted text when a request depends on a website.

Future work: a backend model tool reads one supplied public page's text, favicon and styling cues. Support bare domains and HTTPS paths/queries, normalize and embed the favicon for offline use, and explain unavailable content or branding. No page photos, illustrations, arbitrary logo downloads, crawling, search or login. Bound downloads/elapsed time and validate DNS, each redirect and resource destination to reject private/local addresses and unsupported schemes. Page content is untrusted source material, never tool instructions. Model/tool exchanges share one generation allowance unit. No reader implementation is part of this MVP.

Document-reading tools, image generation and live-data integrations are also outside this MVP.

## Card quality

- Fully transparent outer canvas, with a nearly clear glass panel using 10–20% background alpha (default 16%). Use a thin translucent border, faint highlight and restrained shadow; keep text/icons fully opaque and give controls similarly light tinted backgrounds. Avoid opaque headers, solid fills and blur filters. Keep background alpha at or below 25% unless the user explicitly asks for an opaque background.
- Large, readable text; simple layout; restrained color.
- Fits its bounds and works offline.
- Fully interactive local HTML: buttons, inputs, timers, slides, forms and games can respond to the presenter's mouse and keyboard.
- Use the app's small validated state interface to save meaningful values such as scores, entered text and the current slide. Do not save the JavaScript runtime; see [DATA.md](DATA.md).

## Failures

- Preserve the existing card.
- Show a useful error and retry action.
- Do not automatically regenerate after a dropped connection. Without a locally saved response, the result is lost; offer an explicit new generation and explain that it uses another allowance unit.
- Cancellation discards the result; provider processing may already be underway.

## Implementation details

- Validate structured output, size limits, and image IDs. The desktop assigns identity and retains referenced assets; see [DATA.md](DATA.md).
- Authenticate and reserve allowance first. Repeated request IDs return status only, never another model call or saved output. Use a new ID for a deliberate new generation.
- A generation may include model/tool exchanges. Do not automatically start a second generation to repair output. Record uncertain outcomes without automatically refunding or reissuing them.
- Treat prompts, HTML, and pasted text as untrusted input.
- Enforce browser isolation: no network, external navigation, downloads, arbitrary host access, camera/microphone, or browser storage. Only the validated local state interface is allowed.
- Reject website-dependent prompts before reserving allowance. Do not upload local image bytes, paths, or credentials.

## Verify

- Name tag, agenda, countdown, and image caption.
- Revision preserves images; failure or late results never overwrite newer work.
- A lost response cannot be recovered; repeated IDs do not regenerate or consume another allowance unit. Explicit new generation does.
- Website-dependent prompts ask for pasted text and do not use allowance.
- Saved cards render offline; animation meets [FRAMES.md](FRAMES.md).
- Buttons and keyboard input work after resizing; saved values survive hiding and reopening.
