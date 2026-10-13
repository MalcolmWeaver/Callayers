# Callayers v2

Sign in, choose a webcam, create cards, annotate, and show the result in a call.

## Read the specs

1. [The app](APP.md) — user flow, layout, and behavior.
2. [Creating cards with AI](AI.md) — what the backend generates.
3. [What gets saved](DATA.md) — local data, database, and settings.
4. [Backend](BACKEND.md) — endpoints, telemetry, and deployment.
5. [Video: MVP and architecture](FRAMES.md) — video design, build order, and minimum verification.

These specs describe a fresh implementation. Start with the small video foundation in [FRAMES.md](FRAMES.md), verify it, then build the remaining MVP around it. Git history is reference material and prior evidence, not code to restore or port. Existing performance measurements provide the comparison baseline.

## Reference files

- [Screenshot](references/ui.png): layout reference; remove deck/Save controls and technical status.
- [Annotation bar](references/annotation-bar.png): compact tools and color reference; replace Undo/Redo with Eraser, start with simple swatches.
- `assets/`: PNG, ICO, and SVG logo files.
- [Old WinUI shell](references/winui_shell.h): styling reference only, not code to build or a feature list.

Old source commit: `384e9078d3864d16332178c5cb72776b998ee46d`; pre-cloud baseline: `634e4c3`. Keep the original repository history separately. This folder contains specs and references, not an implementation or full backup.
