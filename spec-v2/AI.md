# Creating cards with AI

**The backend generates. The desktop displays and saves.**

## Create and revise

- Send the prompt and desired card shape.
- For revisions, include the selected card and its image references.
- Return a short name and self-contained HTML/CSS/JS.
- Change what was requested; preserve the rest.
- Keep user-imported images local. Never send webcam frames or other cards.
- After a successful revision, the desktop keeps one previous version. **Restore previous version** works locally without an AI call; see [APP.md](APP.md#everyday-behavior).

## Use a website

- Accept one supplied address: `x.com`, `example.com/about`, or a full HTTPS URL.
- Read its public text.
- When requested, use its colors, typography cues, and logo.
- Approximate the branding; do not promise an exact copy.
- Embed the logo so the saved card works offline.
- If the page is unreadable, ask for pasted text or an imported image.
- If only branding is missing, use the available text and explain the omission.
- Never invent missing content.
- No web search, crawling, or logged-in browsing.

**The backend can retrieve a website. The finished card cannot access the internet.**

## Card quality

- Transparent by default.
- Large, readable text; simple layout; restrained color.
- Fits its bounds and works offline.
- JavaScript only for simple presentation behavior, such as timers or animation.
- Timer and animation behavior belongs to the card's JavaScript. No special restoration API or saved execution state.

## Failures

- Preserve the existing card.
- Show a useful error and retry action.
- Do not automatically regenerate after a dropped connection. Without a locally saved response, the result is lost; offer an explicit new generation and explain that it uses another allowance unit.
- Cancellation discards the result; provider processing may already be underway.

## Implementation details

- Validate structured output, size limits, and image IDs. The desktop assigns identity and retains referenced assets; see [DATA.md](DATA.md).
- Authenticate and reserve allowance first. Repeated request IDs return status only, never another model call or saved output. Use a new ID for a deliberate new generation.
- No automatic second model call to repair output. Record uncertain outcomes without automatically refunding or reissuing them.
- Treat prompts, HTML, and website text as untrusted input.
- Enforce browser isolation: no network, navigation, downloads, host access, camera/microphone, or persistent storage.
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
