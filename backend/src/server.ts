import Fastify from 'fastify';
import { z } from 'zod';
import { config, db, hash, ApiError, rateLimit, accountRef, month } from './core.js';
import { startLogin, approveLogin, exchangeLogin, desktopUser, adminUser, googleUser } from './auth.js';
import { generate, requestStatus, accountStatus } from './ai.js';
import { capture } from './telemetry.js';

const app = Fastify({ bodyLimit: 350000, logger: true, disableRequestLogging: true, trustProxy: false });
app.addHook('onRequest', async (req, reply) => {
  reply.header('Cache-Control', 'no-store').header('X-Content-Type-Options','nosniff');
  // Browser UI is served through Firebase Hosting's same-origin /v1 rewrite. No wildcard CORS.
  if (req.headers.origin && req.headers.origin !== config.PUBLIC_ORIGIN) throw new ApiError(403, 'origin_rejected', 'Unrecognized website.');
});
app.setErrorHandler((error, req, reply) => {
  if (error instanceof z.ZodError) { reply.code(400).send({ code: 'invalid_request', message: 'The request format is invalid.' }); return; }
  const status = error instanceof ApiError ? error.statusCode : (error as { statusCode?: number }).statusCode ?? 500;
  const code = error instanceof ApiError ? error.code : 'service_error';
  // Never log bodies, tokens, provider messages, email addresses, prompts, or full URLs.
  if (status >= 500) app.log.error({ code, requestId: req.id }, 'Request failed');
  reply.code(status).send({ code, message: error instanceof ApiError ? error.message : 'The service is temporarily unavailable. Please try later.' });
});
app.get('/health', async () => ({ ok: true }));
app.post('/v1/login/start', startLogin);
app.post('/v1/login/approve', approveLogin);
app.post('/v1/login/exchange', exchangeLogin);
app.get('/v1/browser/me', async req => { const u = await googleUser(req); return { uid: u.uid, email: u.email, admin: (await db.collection('admins').doc(u.uid).get()).exists }; });
app.get('/v1/account', async req => accountStatus(await desktopUser(req)));
app.post('/v1/logout', async req => {
  await desktopUser(req); const token = req.headers.authorization!.slice(7);
  await db.collection('sessions').doc(hash(token)).delete(); return { signedOut: true };
});
app.post('/v1/generate', async req => generate(await desktopUser(req), req.body));
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
  return { accounts: rows.docs.map(d => ({ uid: d.id, ...d.data() })), nextCursor: rows.size === 50 ? rows.docs.at(-1)!.data().createdAt : null };
});
app.get<{ Params: { uid: string } }>('/v1/admin/accounts/:uid', async req => {
  await adminUser(req); const uid = z.string().min(1).max(128).regex(/^[^/]+$/).parse(req.params.uid);
  const [account, requests] = await Promise.all([accountStatus(uid), db.collection('requests').where('uid','==',uid).orderBy('createdAt','desc').limit(50).get()]);
  return { ...account, requests: requests.docs.map(d => ({ id: d.id, ...d.data() })) };
});
app.patch<{ Params: { uid: string } }>('/v1/admin/accounts/:uid', async req => {
  const actor = await adminUser(req); const uid = z.string().min(1).max(128).regex(/^[^/]+$/).parse(req.params.uid);
  const change = z.object({ approved: z.boolean(), paid: z.boolean(), disabled: z.boolean() }).strict().parse(req.body);
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
