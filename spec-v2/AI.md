# Creating cards with AI

**The backend generates. The desktop displays and saves.** Build sequence: [AI implementation](AI-IMPLEMENTATION.md); overall order: [master MVP plan](README.md#complete-mvp-sequence).

## Create and revise

- Send the prompt. There is no card-shape selector; infer a suitable layout from the requested content and any sizing instructions in the prompt.
- For revisions, include the selected card and its image references.
- Return a short name and self-contained HTML/CSS/JS.
- Change what was requested; preserve the rest.
- Keep user-imported images local. Never send webcam frames or other cards.
- After a successful revision, the desktop keeps one previous version. **Restore previous version** works locally without an AI call; see [APP.md](APP.md#everyday-behavior).

## Use a website

**Website reading is required AI tool use on the backend, not a guess from the URL.** During `/generate`, the model can call a website reader for the supplied page. The tool returns public text, styling cues and requested logo/assets; the model uses those results to summarize or create the card. Website content is source material, never instructions for the tools.

- Accept one supplied address: `x.com`, `example.com/about`, or a full HTTPS URL.
- Read its public text.
- When requested, use its colors, typography cues, and logo.
- Approximate the branding; do not promise an exact copy.
- Embed the logo so the saved card works offline.
- When requested, include relevant page images as embedded assets. Images, stylesheets and logos count toward the same download limits; do not copy the site's scripts or entire asset library.
- If the page is unreadable, ask for pasted text or an imported image.
- If only branding is missing, use the available text and explain the omission.
- Never invent missing content.
- No web search, crawling, or logged-in browsing.

**The backend can retrieve a website. The finished card cannot access the internet.**

Pasted text and ordinary card generation need no external tool. Web search, document-reading tools, image generation and live-data integrations are outside this MVP. Tool calls within one generation share its allowance unit; they are not separate generations.

## Card quality

- Transparent outer canvas by default, with content grouped on a dark translucent panel around 80% background opacity. Text and controls remain fully opaque; use background alpha rather than whole-card opacity. Avoid backdrop blur and honor explicit visual requests.
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
- Treat prompts, HTML, and website text as untrusted input.
- Enforce browser isolation: no network, external navigation, downloads, arbitrary host access, camera/microphone, or browser storage. Only the validated local state interface is allowed.
- Add HTTPS to bare domains. Preserve paths/queries and handle surrounding punctuation. Ignore email addresses; ask which URL to use if several are supplied.
- Fetch inside `/generate`. Limit downloads, redirects, and elapsed time. Check DNS and every resource/redirect destination; reject private/local addresses and unsupported schemes.
- Return normalized website logos as embedded assets within card limits. Do not upload local image bytes, paths, or credentials.

## Verify

- Name tag, agenda, countdown, and image caption.
- Revision preserves images; failure or late results never overwrite newer work.
- A lost response cannot be recovered; repeated IDs do not regenerate or consume another allowance unit. Explicit new generation does.
- Bare/full URLs, paths, queries, and trailing punctuation.
- Blocked pages, missing branding, and unsafe redirects.
- Saved cards and logos render offline; animation meets [FRAMES.md](FRAMES.md).
- Buttons and keyboard input work after resizing; saved values survive hiding and reopening.
