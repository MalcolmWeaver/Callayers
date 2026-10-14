import {createReleaseStore, cleanupReleases} from './release-retention.mjs';

const args = process.argv.slice(2);
if (args.some(arg => arg !== '--dry-run') || args.length > 1) throw Error('Usage: node scripts/cleanup-desktop-releases.mjs [--dry-run]');
const store = createReleaseStore({token: process.env.CLOUDFLARE_API_TOKEN, account: process.env.CLOUDFLARE_ACCOUNT_ID});
await cleanupReleases(store, {dryRun: args.includes('--dry-run')});
