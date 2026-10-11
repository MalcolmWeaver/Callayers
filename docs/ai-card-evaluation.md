# Step 4 live generation checks

Use the same configured model for the first pass. Run these prompts through the app or `AiProbe`; record elapsed time, readable text, fit, transparency, and animation. A saved generated deck must still display with no network connection. Do not count fixture tests as model-quality verification.

1. Create a three-item agenda: Welcome, Live demo, Questions. Mostly transparent, big readable type.
2. Compare remote and in-person workshops in two columns, with three short tradeoffs each.
3. Explain photosynthesis in three numbered steps with simple inline SVG illustrations.
4. Show an illustrative statistic: 72% completion, clearly labeled as demo data, with a progress ring.
5. Draw a simple client → server → database flow diagram, with a subtle moving dot showing a request.
6. Make a 30-second countdown that loops, with a clear timer and gentle progress bar.
7. Display three pricing tiers using these supplied values: Starter $9, Team $29, Studio $59. Label them fictional demo plans.
8. Make a compact project timeline with Discovery, Prototype, Test, Launch. No invented dates.
9. Show a bilingual greeting in English and Japanese, keeping it readable on a transparent background.
10. Make a bar chart using only these demo values: Monday 12, Tuesday 18, Wednesday 9. Label the chart as illustrative.

Then revise the selected agenda: “Keep the placement and transparent style, replace Questions with Next steps, and add a gentle entrance animation.” Confirm the correct card updates, one undo restores the previous HTML, and save/reopen retains the result.

While generation is pending, test selecting another card, moving/resizing the target, deleting it, undoing that deletion, and opening another deck. Movement should be preserved; deleted/restored or reopened targets must reject the old result. Cancel must leave the current card alone. Failed/incomplete/refused responses must not remove it.

Live generation uses OpenAI API billing. `AiProbe --key-status` prints availability only; never print or paste the key into logs or a deck. Configure it in the application's AI settings.

## Website media acceptance

1. Run `AiProbe --fetch https://puppetbooth.com/` to inspect source text, the compact CSS sample, and downloaded asset metadata without an AI call.
2. Generate: “Make a card with three short lines from puppetbooth.com, its logo, and its colors and typography. Keep the background transparent.” Verify each quote against the page, that the actual downloaded logo is used, and that the style resembles the available CSS sample. This reader does not take screenshots or execute the page's JavaScript.
3. Revise the resulting card without a URL: “Keep the logo and wording, make the title smaller.” Verify image identity and bytes survive, and the serialized model request contains asset IDs but no embedded image data.
4. Save/reopen the version-5 deck, then move/resize and undo/redo the revision. Images and the last generation's usage must survive and render offline.
5. Test a missing image, a stylesheet error, an unsupported SVG, and cancellation. Optional failures should show a partial-results notice; cancellation must not apply a new card.
6. Record actual input, cached/cache-write, output, reasoning, and estimated cost from the returned API usage; do not infer actual spend from HTML size. Unknown models must show token counts without a fabricated price.

`WebsiteMediaTests` covers extraction, bounded metadata, URL validation, asset resolution, offline persistence, compact revisions, and usage accounting. `CardProbe` also verifies an embedded SVG card renders after a save/reopen round-trip under the normal network-blocked card policy.
