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
  return Object.freeze({ projectId, databaseId, publicConfig: Object.freeze({
    version: 2, firebaseProjectId: projectId,
    googleDesktopClientId: installed.client_id,
    googleDesktopClientSecret: installed.client_secret,
    firebaseApiKey: env.FIREBASE_WEB_API_KEY,
  }) });
}
