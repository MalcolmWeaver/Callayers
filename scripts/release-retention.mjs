import {RELEASE_BASE_URL, createReleaseStore as createStorageStore} from './release-storage.mjs';
export {RELEASE_BASE_URL} from './release-storage.mjs';
export const RETENTION_KEY = 'tester/retention.json';
export const GRACE_MS = 24 * 60 * 60 * 1000;

export function validVersion(version) {
  return typeof version === 'string' && /^(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})$/.test(version)
    && version.split('.').every(part => +part <= 65535);
}

export function compareVersions(a, b) {
  if (!validVersion(a) || !validVersion(b)) throw Error('Invalid release version.');
  for (let i = 0; i < 3; i++) {
    const difference = +a.split('.')[i] - +b.split('.')[i];
    if (difference) return difference;
  }
  return 0;
}

export function installerKey(version) {
  if (!validVersion(version)) throw Error('Invalid release version.');
  return `releases/${version}/CallayersSetup-${version}.exe`;
}

function installerVersion(key) {
  const match = /^releases\/([^/]+)\/CallayersSetup-([^/]+)\.exe$/.exec(key);
  return match && match[1] === match[2] && validVersion(match[1]) ? match[1] : null;
}

export function feedVersion(envelope) {
  const payload = JSON.parse(Buffer.from(envelope.payload, 'base64').toString('utf8'));
  if (payload.schema !== 1 || payload.channel !== 'tester' || !validVersion(payload.version)
      || payload.url !== `${RELEASE_BASE_URL}/${installerKey(payload.version)}`) {
    throw Error('Invalid published tester feed; cleanup stopped.');
  }
  return payload.version;
}

export function validateRetention(state) {
  if (!state || state.schema !== 1 || !validVersion(state.latestVersion)
      || !(state.previousVersion === null || (validVersion(state.previousVersion)
        && compareVersions(state.previousVersion, state.latestVersion) < 0))
      || !Array.isArray(state.retired)) throw Error('Invalid release retention record.');
  const seen = new Set();
  for (const entry of state.retired) {
    if (!validVersion(entry.version) || entry.version === state.previousVersion
        || compareVersions(entry.version, state.latestVersion) >= 0
        || typeof entry.deleteAfter !== 'string' || !Number.isFinite(Date.parse(entry.deleteAfter))
        || seen.has(entry.version)) throw Error('Invalid retired release record.');
    seen.add(entry.version);
  }
  return state;
}

// Retire releases when they leave the protected pair, rather than using their upload age.
export function prepareRetention(state, version, priorVersion, objects, now = Date.now()) {
  if (!validVersion(version) || (priorVersion !== null && !validVersion(priorVersion))) throw Error('Invalid release version.');
  if (state) validateRetention(state);
  if ((priorVersion && compareVersions(priorVersion, version) > 0)
      || (state && compareVersions(state.latestVersion, version) > 0)) throw Error('Cannot retire a newer release.');
  let previousVersion = priorVersion;
  if (priorVersion === version) {
    // Retrying publication must preserve the previous release and existing deadlines.
    if (!state || state.latestVersion !== version) throw Error('Published release is missing its retention record.');
    previousVersion = state.previousVersion;
  }
  const protectedVersions = new Set([version, previousVersion]);
  const retired = new Map((state?.retired ?? []).map(entry => [entry.version, entry.deleteAfter]));
  // A prior attempt may have saved protection but failed before changing the feed.
  // Restart the grace period in that case; nothing was retired by that failed attempt.
  if (state && state.latestVersion !== priorVersion) {
    for (const [retiredVersion, deadline] of retired) {
      retired.set(retiredVersion, new Date(Math.max(Date.parse(deadline), now + GRACE_MS)).toISOString());
    }
  }
  const versions = objects.map(object => installerVersion(object.key)).filter(Boolean);
  for (const candidate of [state?.latestVersion, state?.previousVersion, ...versions]) {
    if (candidate && !protectedVersions.has(candidate) && compareVersions(candidate, version) < 0 && !retired.has(candidate)) {
      retired.set(candidate, new Date(now + GRACE_MS).toISOString());
    }
  }
  for (const protectedVersion of protectedVersions) retired.delete(protectedVersion);
  return validateRetention({schema: 1, latestVersion: version, previousVersion,
    retired: [...retired].map(([retiredVersion, deleteAfter]) => ({version: retiredVersion, deleteAfter}))});
}

export function createReleaseStore(options) {
  const store = createStorageStore(options);
  return {...store,
    async writeRetention(state) { validateRetention(state); await store.writeRetention(state); },
    async deleteInstaller(version) { await store.deleteObject(installerKey(version)); }
  };
}

export async function cleanupReleases(store, {dryRun = false, now = Date.now(), log = console.log} = {}) {
  const feed = await store.readJson('tester/latest.json');
  if (!feed) { log('No tester release published; nothing to clean up.'); return; }
  const version = feedVersion(feed);
  const state = validateRetention(await store.readJson(RETENTION_KEY));
  if (state.latestVersion !== version) throw Error('Feed and retention record disagree; cleanup stopped.');
  const objects = await store.listInstallers();
  const keys = new Set(objects.map(object => object.key));
  for (const protectedVersion of [version, state.previousVersion].filter(Boolean)) {
    if (!keys.has(installerKey(protectedVersion))) throw Error('A protected installer is missing; cleanup stopped.');
  }
  const expired = state.retired.filter(entry => Date.parse(entry.deleteAfter) <= now);
  log(`Keeping latest ${version}${state.previousVersion ? ` and previous ${state.previousVersion}` : ''}.`);
  for (const entry of expired) {
    if (dryRun) { log(`Would delete ${installerKey(entry.version)}.`); continue; }
    // GitHub serializes cleanup with publication. Also stop if an external publisher changed metadata.
    const currentFeed = await store.readJson('tester/latest.json');
    const currentState = await store.readJson(RETENTION_KEY);
    if (!currentFeed || feedVersion(currentFeed) !== version || JSON.stringify(currentState) !== JSON.stringify(state)) {
      throw Error('Release metadata changed during cleanup; cleanup stopped.');
    }
    await store.deleteInstaller(entry.version);
    log(`Deleted ${installerKey(entry.version)}.`);
  }
  if (!dryRun && expired.length) {
    const currentFeed = await store.readJson('tester/latest.json');
    const currentState = await store.readJson(RETENTION_KEY);
    if (!currentFeed || feedVersion(currentFeed) !== version || JSON.stringify(currentState) !== JSON.stringify(state)) {
      throw Error('Release metadata changed during cleanup; retention record was not overwritten.');
    }
    const deleted = new Set(expired.map(entry => entry.version));
    await store.writeRetention({...state, retired: state.retired.filter(entry => !deleted.has(entry.version))});
  }
  log(`${dryRun ? 'Previewed' : 'Cleaned up'} ${expired.length} expired installer(s); ${state.retired.length - expired.length} within the grace period.`);
}
