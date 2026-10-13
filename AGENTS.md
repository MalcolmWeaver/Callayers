# Feature building and verification

Build small improvements with observable outcomes.

## Feature verification

When a feature is implemented and its build is ready, include a short **Verification: <feature>** section in the chat response. Do not create a separate verification document or checklist file.

Include only:

- A one-sentence goal describing the new behavior.
- The exact launch command, if needed.
- A short numbered list of actions and expected new behavior: **“Do this. You should see this.”**

Usually use three checks or fewer. Add another only when it confirms a distinct behavior affected by the change. If verification grows substantially, split the improvement.

Exclude implementation details, build hashes, result logs, scope explanations, unchanged behavior, agent responsibilities and closure checkboxes.

The user performs runtime verification; the agent prepares builds and fixes failures. Present all checks together so the user can run them independently. If the user wants a guided walkthrough, guide one check at a time and wait for their observation. Fix reported failures. Accept an explicit statement that verification is done without requiring a closing questionnaire. Do not invent observations or measurements.

Keep observations in the conversation. The agent records the accepted outcome and tested build in Git. If a fix needs another build, provide updated verification steps in chat when that build is ready, focused on the affected behavior.

Add resource measurements only when needed to assess a concrete usability concern.

## Model recommendations

These are recommended starting points when the models are available. The user controls model selection in the chat; instructions in this file do not automatically switch models.

| Work | Model and reasoning effort |
| --- | --- |
| Define the feature and its verification plan | GPT-6.1 Sol · medium |
| Build the feature or diagnose a failure | GPT-6.1 Sol · high |
| Guide the agreed walkthrough and read straightforward reports | GPT-6 Luna · low |
| Interpret unexpected results or decide what needs fixing | GPT-6.1 Sol · medium; high for difficult diagnosis |
| A particularly difficult problem that Sol has not resolved | GPT-6 Astra · high |

Recommend switching at phase boundaries, not after every message. Sol prepares the plan; Luna guides it; an unexpected result warrants returning to Sol. Flag the transition for the user. Do not claim a model switch occurred unless it actually did.
