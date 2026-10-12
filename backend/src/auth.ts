import type { FastifyRequest } from 'fastify';
import { z } from 'zod';
import { db, auth, config, hash, secret, expires, accountRef, newAccount, ApiError, rateLimit } from './core.js';

export async function googleUser(req: FastifyRequest) {
  const token = req.headers.authorization?.replace(/^Bearer /, '');
  if (!token) throw new ApiError(401, 'sign_in_required', 'Sign in with Google.');
  try {
    const user = await auth.verifyIdToken(token, true);
    if (!user.email_verified || user.firebase.sign_in_provider !== 'google.com') throw new Error('provider');
    return user;
  } catch { throw new ApiError(401, 'sign_in_required', 'Sign in with Google again.'); }
}
export async function desktopUser(req: FastifyRequest) {
  const token = req.headers.authorization?.replace(/^Bearer /, '') ?? '';
  if (!/^[a-f0-9]{64}$/.test(token)) throw new ApiError(401, 'sign_in_required', 'Sign in from File > Account.');
  const session = (await db.collection('sessions').doc(hash(token)).get()).data();
  if (!session || session.expiresAt.toMillis() <= Date.now()) throw new ApiError(401, 'session_expired', 'Your session expired. Sign in again.');
  const account = (await accountRef(session.uid).get()).data();
  if (!account || account.disabled) throw new ApiError(403, 'account_disabled', 'AI access for this account is disabled. Contact support.');
  return session.uid as string;
}
export async function adminUser(req: FastifyRequest) {
  const uid = await desktopUser(req);
  if (!(await db.collection('admins').doc(uid).get()).exists) throw new ApiError(403, 'admin_required', 'This Google account is not an administrator.');
  return uid;
}

export async function startLogin(req: FastifyRequest) {
  if (!config.PUBLIC_ORIGIN || !config.FIREBASE_WEB_API_KEY) throw new ApiError(503, 'login_unconfigured', 'Sign-in configuration is not complete yet.');
  await rateLimit('login:' + req.ip, 12, 60000);
  const { challenge } = z.object({ challenge: z.string().regex(/^[a-f0-9]{64}$/) }).strict().parse(req.body);
  const id = secret();
  await db.collection('logins').doc(id).create({ challenge, expiresAt: expires(10 * 60000), uid: null });
  return { id, code: id.slice(0, 8).toUpperCase(), url: config.PUBLIC_ORIGIN + '/login.html#' + id };
}
export async function approveLogin(req: FastifyRequest) {
  const user = await googleUser(req);
  await rateLimit('approve:' + user.uid, 20, 60000);
  const { id } = z.object({ id: z.string().regex(/^[a-f0-9]{64}$/) }).strict().parse(req.body);
  await db.runTransaction(async tx => {
    const ref = db.collection('logins').doc(id); const login = (await tx.get(ref)).data();
    const account = await tx.get(accountRef(user.uid));
    if (!login || login.expiresAt.toMillis() < Date.now() || login.uid) throw new ApiError(409, 'login_expired', 'Start sign-in again in Callayers.');
    if (account.data()?.disabled) throw new ApiError(403, 'account_disabled', 'This account is disabled.');
    if (!account.exists) tx.create(accountRef(user.uid), newAccount(user.email!));
    tx.update(ref, { uid: user.uid });
  });
  return { approved: true };
}
export async function exchangeLogin(req: FastifyRequest) {
  const { id, proof } = z.object({ id: z.string().regex(/^[a-f0-9]{64}$/), proof: z.string().regex(/^[a-f0-9]{64}$/) }).strict().parse(req.body);
  // Only the initiating desktop knows this high-entropy proof. It never enters a URL.
  return db.runTransaction(async tx => {
    const loginRef = db.collection('logins').doc(id); const login = (await tx.get(loginRef)).data();
    if (!login || login.expiresAt.toMillis() < Date.now() || login.challenge !== hash(proof)) throw new ApiError(401, 'login_expired', 'Start sign-in again in Callayers.');
    if (!login.uid) return { pending: true };
    if (!login.claimed) {
      tx.create(db.collection('sessions').doc(hash(proof)), { uid: login.uid, expiresAt: expires(30 * 86400000), createdAt: Date.now() });
      tx.update(loginRef, { claimed: true });
    }
    // A retry after a lost response confirms the same session; it never mints another one.
    return { pending: false, uid: login.uid };
  });
}
