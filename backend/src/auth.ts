import type { FastifyRequest } from 'fastify';
import { db, auth, accountRef, newAccount, ApiError } from './core.js';

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
  const user = await googleUser(req);
  const ref = accountRef(user.uid);
  let account = (await ref.get()).data();
  if (!account) {
    account = await db.runTransaction(async tx => {
      const existing = (await tx.get(ref)).data();
      if (existing) return existing;
      const created = newAccount(user.email!); tx.create(ref, created); return created;
    });
  }
  if (account.disabled) throw new ApiError(403, 'account_disabled', 'This account is disabled. Contact support.');
  return user.uid;
}
export async function adminUser(req: FastifyRequest) {
  const uid = await desktopUser(req);
  if (!(await db.collection('admins').doc(uid).get()).exists) throw new ApiError(403, 'admin_required', 'This Google account is not an administrator.');
  return uid;
}

