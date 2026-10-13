import { getApps, initializeApp } from 'firebase/app';
import {
  browserLocalPersistence, createUserWithEmailAndPassword, getAuth,
  getRedirectResult, GoogleAuthProvider, onAuthStateChanged, setPersistence,
  signInWithEmailAndPassword, signInWithRedirect,
  signOut as firebaseSignOut, updateProfile, type Auth, type Unsubscribe, type User
} from 'firebase/auth';

export type AccountUser = { uid: string; email: string; name: string };
export type AuthErrorCode = 'EMAIL_TAKEN' | 'INVALID_CREDENTIALS' | 'AUTH_UNAVAILABLE';

export class AuthServiceError extends Error {
  constructor(public readonly code: AuthErrorCode, message: string) { super(message); }
}

let authPromise: Promise<Auth> | undefined;
function clientAuth(): Promise<Auth> {
  authPromise ??= (async () => {
    const response = await fetch('/v1/client-config');
    if (!response.ok) throw new Error('Sign-in configuration is not complete yet.');
    const existing = getApps().find(app => app.name === 'callayers-web-account');
    const auth = getAuth(existing ?? initializeApp(await response.json(), 'callayers-web-account'));
    await setPersistence(auth, browserLocalPersistence);
    return auth;
  })().catch(error => { authPromise = undefined; throw error; });
  return authPromise;
}

function account(user: User): AccountUser {
  return { uid: user.uid, email: user.email ?? '', name: user.displayName || user.email || 'Account' };
}

function authError(error: unknown): AuthServiceError {
  const code = (error as { code?: string })?.code;
  if (code === 'auth/email-already-in-use') return new AuthServiceError('EMAIL_TAKEN', 'Email already registered.');
  if (['auth/invalid-credential', 'auth/user-not-found', 'auth/wrong-password', 'auth/invalid-email'].includes(code ?? ''))
    return new AuthServiceError('INVALID_CREDENTIALS', 'Invalid email or password.');
  return new AuthServiceError('AUTH_UNAVAILABLE', 'Sign-in is unavailable. Please try again.');
}

export async function signIn({ email, password }: { email: string; password: string }): Promise<AccountUser> {
  try { return account((await signInWithEmailAndPassword(await clientAuth(), email.trim(), password)).user); }
  catch (error) { throw authError(error); }
}

export async function signInWithGoogle(): Promise<void> {
  try {
    const provider = new GoogleAuthProvider();
    provider.setCustomParameters({ prompt: 'select_account' });
    await signInWithRedirect(await clientAuth(), provider);
  } catch (error) { throw authError(error); }
}

export async function finishGoogleSignIn(): Promise<void> {
  try { await getRedirectResult(await clientAuth()); }
  catch (error) { throw authError(error); }
}

export async function signUp({ name, email, password }: { name: string; email: string; password: string }): Promise<AccountUser> {
  try {
    const result = await createUserWithEmailAndPassword(await clientAuth(), email.trim(), password);
    await updateProfile(result.user, { displayName: name.trim() });
    return account(result.user);
  } catch (error) { throw authError(error); }
}

export async function signOut(): Promise<void> {
  try { await firebaseSignOut(await clientAuth()); }
  catch (error) { throw authError(error); }
}

export async function getCurrentUser(): Promise<AccountUser | null> {
  const auth = await clientAuth();
  await auth.authStateReady();
  return auth.currentUser ? account(auth.currentUser) : null;
}

export async function onAuthChange(callback: (user: AccountUser | null) => void): Promise<Unsubscribe> {
  return onAuthStateChanged(await clientAuth(), user => callback(user ? account(user) : null));
}
