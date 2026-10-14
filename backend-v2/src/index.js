import { createServer } from 'node:http';
import { initializeApp, applicationDefault } from 'firebase-admin/app';
import { getAuth } from 'firebase-admin/auth';
import { getFirestore, Timestamp } from 'firebase-admin/firestore';
import { loadConfig } from './config.js';
import { accountStore } from './accounts.js';
import { createHandler } from './server.js';
import { generationProvider } from './generation.js';
import { telemetryProvider } from './telemetry.js';

const config = loadConfig(process.env);
const app = initializeApp({ projectId: config.projectId, credential: applicationDefault() });
const db = getFirestore(app, config.databaseId);
const server = createServer({ maxHeaderSize: 24576, requestTimeout: 15000, headersTimeout: 10000 }, createHandler({
  config, auth: getAuth(app), accounts: accountStore(db, () => Timestamp.now(), date=>Timestamp.fromDate(date)),
  generate:config.openaiKey?generationProvider(config):undefined,
  telemetry:config.posthogToken?telemetryProvider(config):undefined,
  log: event => console.error(JSON.stringify({ severity: 'ERROR', event })),
}));
server.listen(Number(process.env.PORT || 8080), '0.0.0.0');
process.on('SIGTERM', () => server.close(() => process.exit(0)));
