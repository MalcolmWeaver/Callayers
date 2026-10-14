// Installed-app OAuth configuration is public client configuration, never a
// service-account key. Server credentials come from Cloud Run's service identity.
export function loadConfig(env) {
  const projectId = env.GOOGLE_CLOUD_PROJECT;
  const databaseId = env.FIRESTORE_DATABASE_ID;
  if (!projectId || databaseId !== 'callayers-v2') throw new Error('Invalid v2 database configuration');
  let installed;
  try { installed = JSON.parse(env.GOOGLE_DESKTOP_CLIENT_JSON).installed; } catch {}
  if (!installed || installed.project_id !== projectId ||
      typeof installed.client_id !== 'string' || !installed.client_id.endsWith('.apps.googleusercontent.com') ||
      typeof installed.client_secret !== 'string' || !installed.client_secret ||
      typeof env.FIREBASE_WEB_API_KEY !== 'string' || !env.FIREBASE_WEB_API_KEY) {
    throw new Error('Missing installed-app OAuth configuration');
  }
  const openaiKey=(env.OPENAI_API_KEY||'').trim();
  const dailyLimit=env.USER_DAILY_GENERATIONS === undefined ? null : Number(env.USER_DAILY_GENERATIONS);
  if ((dailyLimit !== null && (!Number.isSafeInteger(dailyLimit)||dailyLimit<1||dailyLimit>1000)) ||
      (openaiKey && dailyLimit === null)) throw new Error('Set USER_DAILY_GENERATIONS to an integer from 1 to 1000');
  const aiModel=(env.AI_MODEL||'').trim();
  if ((openaiKey && !aiModel) || (aiModel && !/^[A-Za-z0-9_.:-]{1,128}$/.test(aiModel)))
    throw new Error('Set AI_MODEL to the intended provider model');
  const aiReasoningEffort=(env.AI_REASONING_EFFORT||'').trim()||null;
  if (aiReasoningEffort && !['none','minimal','low','medium','high','xhigh','max'].includes(aiReasoningEffort))
    throw new Error('Invalid AI_REASONING_EFFORT');
  return Object.freeze({ projectId, databaseId, dailyLimit, openaiKey, aiModel, aiReasoningEffort, publicConfig: Object.freeze({
    version: 2, firebaseProjectId: projectId,
    googleDesktopClientId: installed.client_id,
    googleDesktopClientSecret: installed.client_secret,
    firebaseApiKey: env.FIREBASE_WEB_API_KEY,
  }) });
}
