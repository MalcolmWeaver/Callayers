# Feature building and verification

Build small improvements with observable outcomes.

## Feature verification

Create a verification Markdown file only when a build is ready and the user can verify it now. The file's existence means verification is the current action. Do not use it for implementation planning or status reporting.

Include only:

- A short feature title.
- The exact launch command, if needed.
- A flat checklist of actions and expected new behavior: **“Do this. You should see this.”**

Usually use three checks or fewer. Add another only when it confirms a distinct behavior affected by the change. If verification grows substantially, split the improvement.

Exclude implementation details, build hashes, result logs, scope explanations, unchanged behavior, agent responsibilities and closure checkboxes.

The user performs runtime verification; the agent prepares builds and fixes failures. Guide one check at a time in chat and wait for the user's observation. Fix reported failures. Accept an explicit statement that verification is done without requiring a closing questionnaire. Do not invent observations or measurements.

The agent records the accepted outcome and tested build in Git, then removes the verification file. If further implementation becomes the current action, remove the file until a replacement build is ready; retain observations in the task history.

Add resource measurements only when needed to assess a concrete usability concern.

## Model recommendations

These are recommended starting points when the models are available. The user controls model selection in the chat; instructions in this file do not automatically switch models.

| Work | Model and reasoning effort |
| --- | --- |
| Define the feature and its verification plan | GPT-6.1 Sol · medium |
| Build the feature or diagnose a failure | GPT-6.1 Sol · high |
| Guide the agreed walkthrough, read straightforward reports, update checkboxes | GPT-6 Luna · low |
| Interpret unexpected results or decide what needs fixing | GPT-6.1 Sol · medium; high for difficult diagnosis |
| A particularly difficult problem that Sol has not resolved | GPT-6 Astra · high |

Recommend switching at phase boundaries, not after every message. Sol prepares the plan; Luna guides it; an unexpected result warrants returning to Sol. Flag the transition for the user. Do not claim a model switch occurred unless it actually did.
