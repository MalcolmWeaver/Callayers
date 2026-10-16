import {readFile, appendFile} from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {validVersion, compareVersions, feedVersion, RELEASE_BASE_URL} from './release-retention.mjs';

const feeds = {
  windows: `${RELEASE_BASE_URL}/tester/latest.json`,
  linux: `${RELEASE_BASE_URL}/linux/apt/dists/stable/InRelease`
};
function feedUrl(platform) {
  if (!Object.hasOwn(feeds, platform)) throw Error('Choose windows or linux.');
  return feeds[platform];
}

export function incrementVersion(current, increment) {
  if (!validVersion(current)) throw Error('Invalid current version.');
  const index = ['major', 'minor', 'patch'].indexOf(increment);
  if (index < 0) throw Error('Choose major, minor, or patch.');
  const parts = current.split('.').map(Number);
  parts[index]++;
  for (let i = index + 1; i < parts.length; i++) parts[i] = 0;
  const version = parts.join('.');
  if (!validVersion(version)) throw Error('Selected increment exceeds the release version limit.');
  return version;
}

export async function publishedVersion(platform, {fetchImpl = fetch} = {}) {
  const response = await fetchImpl(feedUrl(platform), {
    cache: 'no-store', redirect: 'error', signal: AbortSignal.timeout(15000)
  });
  if (response.status === 404) return null;
  if (response.status !== 200) throw Error(`Cannot read published ${platform} version (${response.status}); stopped.`);
  const text = await response.text();
  if (Buffer.byteLength(text) > 65536) throw Error('Published release metadata is too large.');
  if (platform === 'windows') return feedVersion(JSON.parse(text));
  const versions = [...text.matchAll(/^X-Callayers-Version: (.+)$/gm)];
  if (!text.startsWith('-----BEGIN PGP SIGNED MESSAGE-----\n') || versions.length !== 1 || !validVersion(versions[0][1]))
    throw Error('Invalid published Linux version; stopped.');
  // This reads the version over HTTPS, not installer trust. APT and the Windows
  // updater still verify release signatures and package hashes independently.
  return versions[0][1];
}

export async function selectVersion(platform, increment, options = {}) {
  feedUrl(platform);
  // Check the choice before doing any network work.
  incrementVersion('0.0.0', increment);
  const current = await publishedVersion(platform, options);
  let seed = null;
  if (current === null) {
    const source = await readFile(new URL(platform === 'linux' ? '../linux/CMakeLists.txt' : '../CMakeLists.txt', import.meta.url), 'utf8');
    seed = source.match(platform === 'linux' ? /^set\(CALLAYERS_RELEASE_VERSION "([^"]+)"/m : /^set\(CALLAYERS_VERSION "([^"]+)"/m)?.[1];
    if (!validVersion(seed)) throw Error('Missing first-release starting version.');
  }
  return {platform, current, seed, increment, version: incrementVersion(current ?? seed, increment), source: feedUrl(platform)};
}

export async function checkCandidate(platform, version, options = {}) {
  feedUrl(platform);
  if (!validVersion(version)) throw Error('Invalid candidate version.');
  const current = await publishedVersion(platform, options);
  if (current && compareVersions(version, current) < 0) throw Error('A newer release is already published. Prepare and test a new candidate.');
  // Equal versions may retry an interrupted publication. The publisher still
  // rejects any attempt to replace immutable package bytes.
  return {platform, current, version, source: feedUrl(platform)};
}

export async function reportVersion(result, {env = process.env, log = console.log} = {}) {
  const label = result.platform === 'linux' ? 'Linux' : 'Windows';
  const baseline = result.current ?? `not published (starting point ${result.seed ?? 'none'})`;
  log(`${label}: current ${baseline}; selected ${result.version}${result.increment ? ` (${result.increment})` : ''}`);
  if (env.GITHUB_OUTPUT) await appendFile(env.GITHUB_OUTPUT, `current=${result.current ?? ''}\nversion=${result.version}\n`);
  if (env.GITHUB_ENV) await appendFile(env.GITHUB_ENV, `VERSION=${result.version}\n`);
  if (env.GITHUB_STEP_SUMMARY) await appendFile(env.GITHUB_STEP_SUMMARY,
    `### ${label} release version\n\nCurrent published version: **${result.current ?? 'None'}**\n\n` +
    (result.seed ? `First-release starting point: **${result.seed}** (CMake default; used only after HTTP 404).\n\n` : '') +
    `Selected version: **${result.version}**${result.increment ? ` (${result.increment})` : ''}\n\n` +
    `Version source: ${result.source}\n\n`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const [action, platform, value, ...extra] = process.argv.slice(2);
  if (extra.length || !['select', 'check'].includes(action))
    throw Error('Usage: node scripts/release-version.mjs select <windows|linux> <patch|minor|major> OR check <windows|linux> <version>');
  const result = action === 'select' ? await selectVersion(platform, value) : await checkCandidate(platform, value);
  await reportVersion(result);
}
