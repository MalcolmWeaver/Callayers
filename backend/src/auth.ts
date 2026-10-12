import type { FastifyRequest } from 'fastify';
import { z } from 'zod';
import { db, auth, config, hash, secret, expires, accountRef, newAccount, ApiError, rateLimit } from './core.js';
import { startLoginBody, exchangeLoginBody, desktopReturnUrl } from './login-protocol.js';

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
  const { challenge, desktopReturn } = startLoginBody.parse(req.body);
  const id = secret();
  await db.collection('logins').doc(id).create({ challenge, expiresAt: expires(10 * 60000), uid: null,
    ...(desktopReturn ? { desktopReturn } : {}) });
  return { id, code: id.slice(0, 8).toUpperCase(), url: config.PUBLIC_ORIGIN + '/login.html#' + id + (desktopReturn ? '&desktop=1' : '') };
}
export async function approveLogin(req: FastifyRequest) {
  const user = await googleUser(req);
  await rateLimit('approve:' + user.uid, 20, 60000);
  const { id } = z.object({ id: z.string().regex(/^[a-f0-9]{64}$/) }).strict().parse(req.body);
  return db.runTransaction(async tx => {
    const ref = db.collection('logins').doc(id); const login = (await tx.get(ref)).data();
    const account = await tx.get(accountRef(user.uid));
    if (!login || login.expiresAt.toMillis() < Date.now() || login.uid) throw new ApiError(409, 'login_expired', 'Start sign-in again in Callayers.');
    if (account.data()?.disabled) throw new ApiError(403, 'account_disabled', 'This account is disabled.');
    if (!account.exists) tx.create(accountRef(user.uid), newAccount(user.email!));
    const grant = login.desktopReturn ? secret() : undefined;
    tx.update(ref, { uid: user.uid, ...(grant ? { grantHash: hash(grant) } : {}) });
    return { approved: true, ...(grant ? { returnUrl: desktopReturnUrl(login.desktopReturn, grant) } : {}) };
  });
}
export async function exchangeLogin(req: FastifyRequest) {
  const { id, proof, grant } = exchangeLoginBody.parse(req.body);
  // Only the initiating desktop knows this high-entropy proof. It never enters a URL.
  return db.runTransaction(async tx => {
    const loginRef = db.collection('logins').doc(id); const login = (await tx.get(loginRef)).data();
    if (!login || login.expiresAt.toMillis() < Date.now() || login.challenge !== hash(proof)) throw new ApiError(401, 'login_expired', 'Start sign-in again in Callayers.');
    if (!login.uid) return { pending: true };
    // Browser approval alone cannot unlock an automatic login. The initiating
    // desktop must also receive the grant through its private loopback listener.
    if (login.desktopReturn && (!grant || login.grantHash !== hash(grant)))
      throw new ApiError(401, 'login_return_required', 'Return to the desktop that started sign-in.');
    if (!login.claimed) {
      tx.create(db.collection('sessions').doc(hash(proof)), { uid: login.uid, expiresAt: expires(30 * 86400000), createdAt: Date.now() });
      tx.update(loginRef, { claimed: true });
    }
    // A retry after a lost response confirms the same session; it never mints another one.
    return { pending: false, uid: login.uid };
  });
}
