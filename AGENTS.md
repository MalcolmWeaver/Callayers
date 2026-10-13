# Feature building and verification

Build for useful outcomes a person can understand. Keep each feature small enough that the user can clearly confirm we accomplished what we intended. If verification becomes sprawling, rethink or split the feature.

Before verification, explain the few high-level outcomes we need to confirm and where the walkthrough ends. Create a short feature-verification Markdown file with nested checkboxes so scope and progress remain visible.

Guide the user through one concrete action at a time. Include exact commands for the specific build and explain the expected result. Wait for their observation, then update the checklist. The user performs runtime verification; the agent prepares builds, reads results, and fixes failures.

Include resource measurements when they matter to usability. Reuse evidence when it answers multiple checks. Distinguish “measured” from “acceptable,” and never assume a pass.

Keep standards high: fix broken behavior or missing core functionality. Add checks only for a concrete reason. Finish by summarizing what works, what needs improvement, and asking whether the verification process was clear and manageable.

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
