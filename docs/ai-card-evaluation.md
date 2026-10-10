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
