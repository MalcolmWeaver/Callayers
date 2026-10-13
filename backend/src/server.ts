import Fastify from 'fastify';
import { readFileSync } from 'node:fs';
import { z } from 'zod';
import { config, db, ApiError, rateLimit, accountRef, month } from './core.js';
import { desktopUser, adminUser, googleUser } from './auth.js';
import { generate, requestStatus, accountStatus } from './ai.js';
import { capture } from './telemetry.js';
import { registerAuthProxy } from './auth-proxy.js';
import { saveSetup } from './setup.js';

const app = Fastify({ bodyLimit: 350000, logger: true, disableRequestLogging: true, trustProxy: false });
app.addHook('onRequest', async (req, reply) => {
  reply.header('Cache-Control', 'no-store').header('X-Content-Type-Options','nosniff');
  // The browser sign-in page and API share this Cloud Run origin. No wildcard CORS.
  // Provider callbacks may be cross-origin. This exception only reaches the
  // isolated Firebase helper proxy, never our account or login APIs.
  if (!req.url.startsWith('/__/auth/') && req.headers.origin && req.headers.origin !== config.PUBLIC_ORIGIN) throw new ApiError(403, 'origin_rejected', 'Unrecognized website.');
});
app.setErrorHandler((error, req, reply) => {
  if (error instanceof z.ZodError) { reply.code(400).send({ code: 'invalid_request', message: 'The request format is invalid.' }); return; }
  const status = error instanceof ApiError ? error.statusCode : (error as { statusCode?: number }).statusCode ?? 500;
  const code = error instanceof ApiError ? error.code : 'service_error';
  // Never log bodies, tokens, provider messages, email addresses, prompts, or full URLs.
  if (status >= 500) app.log.error({ code, requestId: req.id }, 'Request failed');
  reply.code(status).send({ code, message: error instanceof ApiError ? error.message : 'The service is temporarily unavailable. Please try later.' });
});
// Serve only these bundled files; arbitrary paths never reach the filesystem.
for (const [route, file, type] of [
  ['/', 'login.html', 'text/html; charset=utf-8'],
  ['/login.html', 'login.html', 'text/html; charset=utf-8'],
  ['/signin', 'signin.html', 'text/html; charset=utf-8'],
  ['/signup', 'signup.html', 'text/html; charset=utf-8'],
  ['/home', 'home.html', 'text/html; charset=utf-8'],
  ['/app.js', 'app.js', 'text/javascript; charset=utf-8'],
  ['/account.js', 'account.js', 'text/javascript; charset=utf-8'],
  ['/style.css', 'style.css', 'text/css; charset=utf-8']
] as const) {
  const bytes = readFileSync(new URL('../web-dist/' + file, import.meta.url));
  app.get(route, async (_req, reply) => reply.type(type)
    // Origin-only policy preserves Origin on the HTTPS-to-loopback form POST.
    .header('Referrer-Policy', 'origin').header('X-Frame-Options', 'DENY')
    .header('Content-Security-Policy', "default-src 'self'; script-src 'self' https://apis.google.com; connect-src 'self' https://*.googleapis.com https://*.firebaseapp.com; frame-src 'self' https://*.firebaseapp.com https://accounts.google.com; img-src 'self' https://*.googleusercontent.com; style-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self' http://127.0.0.1:*")
    .send(bytes));
}
await registerAuthProxy(app, config.GOOGLE_CLOUD_PROJECT);
app.get('/v1/client-config', async () => {
  if (!config.FIREBASE_WEB_API_KEY || !config.PUBLIC_ORIGIN) throw new ApiError(503, 'login_unconfigured', 'Sign-in configuration is not complete yet.');
  return { apiKey: config.FIREBASE_WEB_API_KEY, projectId: config.GOOGLE_CLOUD_PROJECT, authDomain: new URL(config.PUBLIC_ORIGIN).host };
});
app.get('/health', async () => ({ ok: true }));
app.get('/v1/browser/me', async req => { const u = await googleUser(req); return { uid: u.uid, email: u.email }; });
app.get('/v1/account', async req => {
  const uid = await desktopUser(req);
  return { ...await accountStatus(uid), administrator: (await db.collection('admins').doc(uid).get()).exists };
});
app.post('/v1/generate', async req => generate(await desktopUser(req), req.body));
app.put('/v1/account/setup', async req => saveSetup(await desktopUser(req), req.body));
app.get<{ Params: { id: string } }>('/v1/requests/:id', async req => {
  const uid = await desktopUser(req); await rateLimit('poll:' + uid, 60, 60000); return requestStatus(uid, req.params.id);
});
app.post('/v1/events', async req => capture(await desktopUser(req), req.body));
app.get('/v1/admin/accounts', async req => {
  await adminUser(req);
  const { email, cursor } = z.object({ email: z.string().email().optional(), cursor: z.coerce.number().optional() }).parse(req.query);
  let query = email ? db.collection('accounts').where('email','==',email) : db.collection('accounts').orderBy('createdAt','desc');
  if (!email && cursor) query = query.startAfter(cursor);
  const rows = await query.limit(50).get();
  return { accounts: rows.docs.map(d => ({ uid: d.id, ...d.data() })), nextCursor: !email && rows.size === 50 ? rows.docs.at(-1)!.data().createdAt : null };
});
app.get<{ Params: { uid: string } }>('/v1/admin/accounts/:uid', async req => {
  await adminUser(req); const uid = z.string().min(1).max(128).regex(/^[^/]+$/).parse(req.params.uid);
  const [account, requests] = await Promise.all([accountStatus(uid), db.collection('requests').where('uid','==',uid).orderBy('createdAt','desc').limit(50).get()]);
  return { ...account, requests: requests.docs.map(d => ({ id: d.id, ...d.data() })) };
});
app.patch<{ Params: { uid: string } }>('/v1/admin/accounts/:uid', async req => {
  const actor = await adminUser(req); const uid = z.string().min(1).max(128).regex(/^[^/]+$/).parse(req.params.uid);
  // Accept the retired field from older desktop builds, but never use it as a gate.
  const { approved: _legacyApproval, ...change } = z.object({ approved: z.boolean().optional(), paid: z.boolean(), disabled: z.boolean() }).strict().parse(req.body);
  if (actor === uid && change.disabled) throw new ApiError(400, 'self_disable', 'Use another administrator to disable your own account.');
  await db.runTransaction(async tx => {
    const ref = accountRef(uid); const old = await tx.get(ref);
    if (!old.exists) throw new ApiError(404, 'account_not_found', 'Account not found.');
    tx.update(ref, change);
    tx.create(db.collection('adminAudit').doc(), { actor, uid, change, before: { approved: old.data()!.approved, paid: old.data()!.paid, disabled: old.data()!.disabled }, at: Date.now() });
  });
  return { updated: true };
});
app.get('/v1/admin/spending', async req => {
  await adminUser(req);
  const [users, global, policy] = await Promise.all([db.collection('accounts').orderBy('totalSpent','desc').limit(50).get(), db.collection('globalUsage').doc(month()).get(), db.doc('controls/ai').get()]);
  return { period: month(), global: global.data() ?? { spent: 0, reserved: 0 }, globalLimitUsd: config.GLOBAL_MONTHLY_USD,
    paused: policy.data()?.disabled ?? false, accounts: users.docs.map(d => ({ uid: d.id, email: d.data().email, totalSpent: d.data().totalSpent,
      approved: d.data().approved, paid: d.data().paid, disabled: d.data().disabled })) };
});
app.post('/v1/admin/ai', async req => {
  const actor = await adminUser(req); const change = z.object({ disabled: z.boolean() }).strict().parse(req.body);
  const batch = db.batch(); batch.set(db.doc('controls/ai'), change); batch.create(db.collection('adminAudit').doc(), { actor, change, at: Date.now() }); await batch.commit();
  return { updated: true };
});
await app.listen({ host: '0.0.0.0', port: config.PORT });
