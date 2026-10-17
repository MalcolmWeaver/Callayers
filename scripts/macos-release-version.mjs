import {readFile, appendFile} from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {incrementVersion} from './release-version.mjs';
import {validVersion, compareVersions, RELEASE_BASE_URL} from './release-retention.mjs';

const arches = ['x86_64', 'arm64'];
const validBuild = value => typeof value === 'string' && /^[1-9]\d{0,14}$/.test(value);

export async function publishedMacRelease(arch, {fetchImpl = fetch} = {}) {
  if (!arches.includes(arch)) throw Error('Unexpected Mac architecture.');
  const response = await fetchImpl(`${RELEASE_BASE_URL}/macos/${arch}/latest.json`, {
    cache: 'no-store', redirect: 'error', signal: AbortSignal.timeout(15000)
  });
  if (response.status === 404) return null;
  if (response.status !== 200) throw Error(`Cannot read published Mac ${arch} version (${response.status}); stopped.`);
  const text = await response.text();
  if (Buffer.byteLength(text) > 8192) throw Error('Mac release metadata is too large.');
  const value = JSON.parse(text);
  if (value.schema !== 1 || value.platform !== 'macos' || value.arch !== arch || value.notarized !== true ||
      !validVersion(value.version) || !validBuild(value.build) || !/^[a-f0-9]{64}$/.test(value.sha256) ||
      !/^\d+\.\d+(?:\.\d+)?$/.test(value.minimumSystemVersion) ||
      value.url !== `${RELEASE_BASE_URL}/macos/releases/${value.version}/${arch}/Callayers-${value.version}-${value.build}-macos-${arch}.zip`)
    throw Error('Invalid published Mac release metadata; stopped.');
  // Version selection reads HTTPS metadata. The publisher separately verifies
  // the existing signed appcasts and refuses replacement of published bytes.
  return {version: value.version, build: value.build};
}

async function sourceVersions() {
  const files = await Promise.all(['AppInfo.plist', 'CameraInfo.plist'].map(name =>
    readFile(new URL(`../macos/Config/${name}`, import.meta.url), 'utf8')));
  const field = (text, name) => text.match(new RegExp(`<key>${name}</key>\\s*<string>([^<]+)</string>`))?.[1];
  const version = field(files[0], 'CFBundleShortVersionString');
  const builds = files.map(text => field(text, 'CFBundleVersion'));
  if (!validVersion(version) || !builds.every(validBuild)) throw Error('Invalid Mac source version/build.');
  return {version, builds};
}

export async function selectMacRelease(increment, options = {}) {
  incrementVersion('0.0.0', increment);
  const [published, source] = await Promise.all([
    Promise.all(arches.map(arch => publishedMacRelease(arch, options))), (options.sourceImpl ?? sourceVersions)()
  ]);
  const versions = published.filter(Boolean);
  const current = versions.map(item => item.version).sort(compareVersions).at(-1) ?? null;
  const maximum = [...source.builds, ...versions.map(item => item.build)].map(BigInt)
    .reduce((a, b) => a > b ? a : b);
  const build = (maximum + 1n).toString();
  if (!validBuild(build)) throw Error('Mac build number limit exceeded.');
  return {current, seed: current === null ? source.version : null,
    version: incrementVersion(current ?? source.version, increment), build};
}

export async function reportMacRelease(result, {env = process.env, log = console.log} = {}) {
  log(`Mac: current ${result.current ?? 'not published'}; selected ${result.version} (${result.build})`);
  if (env.GITHUB_OUTPUT) await appendFile(env.GITHUB_OUTPUT,
    `current=${result.current ?? ''}\nversion=${result.version}\nbuild=${result.build}\n`);
  if (env.GITHUB_STEP_SUMMARY) await appendFile(env.GITHUB_STEP_SUMMARY,
    `### Mac release candidate\n\nCurrent published version: **${result.current ?? 'None'}**\n\n` +
    `Selected version: **${result.version}**, build **${result.build}**\n\n` +
    'Both architectures use this candidate. Check-only runs publish nothing.\n');
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const [increment, ...extra] = process.argv.slice(2);
  if (extra.length) throw Error('Usage: node scripts/macos-release-version.mjs <patch|minor|major>');
  await reportMacRelease(await selectMacRelease(increment));
}
