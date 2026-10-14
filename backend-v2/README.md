# Callayers v2 API

Cloud Run service `callayers-v2-api` uses the named Firestore database `callayers-v2` in Google project `callayers`. Firebase Authentication identities and the installed-app Google OAuth client are shared with the original app; the original service and default database are not migration inputs.

The user deployed generation and accepted the creation/show/+1/Reset check on October 13, 2026. Tested desktop SHA256: `3B052EDFE3D4749A466362C72827CAA5D0B8348D7063F010C8EE80F283B4C0EA`. The user subsequently accepted hide/show/restart restoration and cancellation, plus deployment/verification of the styling/configuration update. Delivery order and acceptance live in the [consolidated plan](../spec-v2/README.md#consolidated-plan--session-handoff) and [AI implementation](../spec-v2/AI-IMPLEMENTATION.md).

The accepted backend update separates runtime settings from deployment and adds the opacity prompt and optional reasoning effort. Backend source commit at acceptance: `d7ec643`. Seventeen synthetic backend tests pass without paid calls or cloud record writes.

## Runtime configuration

Manage these settings directly in Cloud Run. Routine deployment neither reads nor writes them. Backend startup validates configuration; generation requires an explicit model and daily allowance.

| Setting | Behavior |
| --- | --- |
| `AI_MODEL` | Required when the provider secret is bound. No implicit model fallback. |
| `AI_REASONING_EFFORT` | Optional. Unset or empty omits the API parameter and uses the model default. Values must be supported by the chosen model. |
| `USER_DAILY_GENERATIONS` | Required when generation is enabled; integer 1..1000. |
| `GOOGLE_CLOUD_PROJECT`, `FIRESTORE_DATABASE_ID`, `FIREBASE_WEB_API_KEY` | Existing sign-in/database configuration. |
| `GOOGLE_DESKTOP_CLIENT_JSON`, `OPENAI_API_KEY` | Secret Manager bindings configured separately from routine deployment. |

Current service choices are `AI_MODEL=gpt-4.1-mini` and `USER_DAILY_GENERATIONS=10`; reasoning effort is unset. Leave effort unset for a model that does not support it. The accepted values are `none`, `minimal`, `low`, `medium`, `high`, `xhigh`, `max`, but individual models support only a subset. Unsupported combinations cause a provider error; the backend does not substitute a different model or retry.

For example, set the allowance independently:

```powershell
gcloud run services update callayers-v2-api --project=callayers --region=us-central1 --update-env-vars=USER_DAILY_GENERATIONS=10
```

To select a model, replace `MODEL_ID` with the intended provider model:

```powershell
gcloud run services update callayers-v2-api --project=callayers --region=us-central1 --update-env-vars="AI_MODEL=MODEL_ID"
```

For a compatible reasoning model, set effort separately, or remove it to use the model default:

```powershell
gcloud run services update callayers-v2-api --project=callayers --region=us-central1 --update-env-vars=AI_REASONING_EFFORT=low
gcloud run services update callayers-v2-api --project=callayers --region=us-central1 --remove-env-vars=AI_REASONING_EFFORT
```

Configuration changes create a revision of the current code. A later routine code deployment preserves the choices. See [Cloud Run environment variables](https://docs.cloud.google.com/run/docs/configuring/services/environment-variables) and [OpenAI reasoning effort](https://developers.openai.com/api/docs/guides/reasoning).

## Routine deployment

From the repository root:

```powershell
.\scripts\deploy-backend-v2.ps1
```

The script installs pinned dependencies, runs backend tests, publishes source to the configured service, checks health/public configuration and rejection of unsigned requests, and writes the public address beside the workspace executable. It does not set environment variables, bind secrets, change IAM/TTL, configure the runtime identity, public invocation, memory, CPU, scaling or concurrency. `AiModel` and `EnableGeneration` deployment switches are removed. Generation readiness follows backend configuration. The build identity is still supplied because it performs the source build; Node 22 is specified in package metadata.

The existing service is already configured; do not rerun setup for this update. Compatible backend changes require no desktop rebuild. `SERVICE_URL` overrides the desktop address file for explicit development testing; there is no fallback to the old service.

## Explicit setup

`setup-backend-v2.ps1` provisions the database, client-deny rules, identities, IAM and OAuth secret separately from routine deployment. Its full setup requires the downloaded installed-app OAuth JSON; `-BuildOnly` configures only the build permissions. No service-account keys are downloaded or shipped.

For an existing service that needs generation provisioning, explicitly run:

```powershell
.\scripts\setup-backend-v2.ps1 -GenerationOnly
```

This grants the runtime identity access to `callayers-openai`, enables Firestore TTL on `requests.expiresAt`, and binds the secret as `OPENAI_API_KEY`. It does not choose a model, effort or daily allowance. Configure model/allowance directly before binding the provider secret. This mode is unnecessary for the already configured service.

For a new service, run identity/database/OAuth setup first, then perform an explicit initial deployment with the initial service settings below. This is bootstrap configuration, not the routine deployment command:

```powershell
.\scripts\setup-backend-v2.ps1 -OAuthJsonPath 'C:\path\to\downloaded-desktop-client.json'
gcloud run deploy callayers-v2-api --source=backend-v2 --project=callayers --region=us-central1 --build-service-account=projects/callayers/serviceAccounts/callayers-v2-build@callayers.iam.gserviceaccount.com --service-account=callayers-v2-api@callayers.iam.gserviceaccount.com --no-invoker-iam-check --min-instances=0 --max-instances=2 --cpu=1 --memory=512Mi --timeout=90 --concurrency=20 --update-env-vars=GOOGLE_CLOUD_PROJECT=callayers,FIRESTORE_DATABASE_ID=callayers-v2,FIREBASE_WEB_API_KEY=AIzaSyBs0kiC_dtrHJEhCtJ7dvfs9KdGhMV_9nE --update-secrets=GOOGLE_DESKTOP_CLIENT_JSON=callayers-v2-desktop-oauth:latest
```

Then choose model/allowance directly and perform explicit generation setup if needed. Future deployments use the routine script with no configuration flags. Runtime identity has Firebase Auth read access, Firestore access conditional on the v2 database, and access only to required secrets. A separate build identity reads the v2 source prefix, writes the source-deploy artifact repository and operational logs. Client rules deny direct database access. Standard provider and Google Cloud charges apply.

## Generation and card styling

`POST /generate` verifies Firebase identity, revocation, Google provider and account access, then atomically reserves the daily allowance. Repeated request IDs return owner-scoped status without another provider call. There are no automatic provider retries or refunds. Firestore stores only owner/status/timestamps with expiry set 24 hours ahead, never prompts or outputs; TTL cleanup is asynchronous. The account daily count is separate from request cleanup. Provider requests use structured Responses API output and `store:false`. Website-dependent prompts are rejected before reservation; website reading is deferred beyond the MVP.

The generation instructions are in [`src/card-prompt.txt`](src/card-prompt.txt), loaded once from a module-relative path. The default styling uses a transparent outer canvas, nearly clear glass panel with 10–20% background alpha (default 16%), thin translucent border/highlight, fully opaque text/icons and no blur filters. Controls use similarly light background tints. Explicit styling requests can override the visual defaults. Offline/state restrictions remain in the same prompt. Editing this file requires backend deployment and affects newly generated cards only.

## Sign-in acceptance and release items

On October 13, 2026, the user accepted browser sign-in/return to webcam, remembered session/annotations after restart, and sign-out stopping Meet output while preserving local annotations. Tested desktop SHA256: `6E30628CEFE469663EF265D95668982EFCD149EF0B05A5E9612928E41124759C`; original accepted sign-in revision: `callayers-v2-api-00001-z56`.

The desktop uses PKCE and a temporary loopback callback. `/client-config` supplies the installed client ID/secret and Firebase API key; Google treats installed-app secrets as public client configuration. Provider credentials stay on the server. Session credentials are secured with Windows DPAPI. No tokens, prompts, card HTML, annotations or webcam images enter operational logs.

Google Auth Platform Audience is External and publishing is In production, as confirmed by the user. Branding/verification and an external-account/clean-profile check remain release items. Telemetry, trial gates and payments are deferred.

References: [Cloud Run source deployment](https://docs.cloud.google.com/run/docs/deploying-source-code), [Google installed-app OAuth](https://developers.google.com/identity/protocols/oauth2/native-app), [Firebase token verification](https://firebase.google.com/docs/auth/admin/verify-id-tokens), [Firestore TTL](https://docs.cloud.google.com/firestore/native/docs/ttl), [OpenAI structured output](https://developers.openai.com/api/docs/guides/structured-outputs?api-mode=responses).
