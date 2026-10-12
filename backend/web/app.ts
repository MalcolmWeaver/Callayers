import { initializeApp } from 'firebase/app';
import { getAuth, GoogleAuthProvider, signInWithRedirect, getRedirectResult, signOut, setPersistence, browserSessionPersistence } from 'firebase/auth';
import { validateTarget, desktopAction, type DesktopTarget } from './desktop-target';

const $ = (id: string) => document.getElementById(id)!;
const status = (text: string) => { $('status').textContent = text; };
const handle = (fn: () => Promise<void>) => async () => {
  try { await fn(); } catch (e) {
    $('signin').hidden = false;
    status(e instanceof Error ? e.message : 'Something went wrong.');
  }
};
void handle(async () => {
  const storageKey = 'callayers.google-redirect';
  const incoming = location.hash.slice(1);
  let pending: DesktopTarget | undefined;
  if (incoming) {
    sessionStorage.removeItem(storageKey);
    history.replaceState(null, '', location.pathname);
    const params = new URLSearchParams(incoming);
    pending = validateTarget({ port: Number(params.get('port')), state: params.get('state'), started: Date.now(), redirecting: false });
    sessionStorage.setItem(storageKey, JSON.stringify(pending));
  } else {
    const saved = sessionStorage.getItem(storageKey);
    if (saved) {
      try { pending = validateTarget(JSON.parse(saved)); }
      catch (e) { sessionStorage.removeItem(storageKey); throw e; }
    }
  }
  if (!pending) {
    $('signin').hidden = true;
    status('Open File > Account > Sign in with Google in the Windows app to connect it.');
    return;
  }
  const response = await fetch('/v1/client-config');
  if (!response.ok) throw new Error('Sign-in configuration is not complete yet.');
  const auth = getAuth(initializeApp(await response.json()));
  await setPersistence(auth, browserSessionPersistence);
  $('signin').onclick = handle(async () => {
    validateTarget(pending);
    const provider = new GoogleAuthProvider(); provider.setCustomParameters({ prompt: 'select_account' });
    const button = $('signin') as HTMLButtonElement; button.disabled = true;
    try {
      sessionStorage.setItem(storageKey, JSON.stringify({ ...pending, redirecting: true }));
      status('Opening Google account chooser…');
      await signInWithRedirect(auth, provider);
    } finally { button.disabled = false; }
  });
  $('signout').onclick = handle(async () => { await signOut(auth); });
  const credential = await getRedirectResult(auth);
  // A remembered browser account cannot connect a new desktop.
  if (!incoming && pending.redirecting && credential) {
    const token = credential.user.refreshToken;
    if (!token) throw new Error('Google sign-in did not finish. Please try again.');
    const form = document.createElement('form');
    form.method = 'POST'; form.action = desktopAction(pending);
    const input = document.createElement('input');
    input.type = 'hidden'; input.name = 'refresh_token'; input.value = token;
    form.appendChild(input); document.body.appendChild(form);
    // Transfer only in a POST body to loopback, never a URL or Firestore.
    await signOut(auth);
    sessionStorage.removeItem(storageKey);
    status('Returning to Callayers…'); $('signin').hidden = true;
    form.submit();
    return;
  }
  if (incoming) { $('signin').hidden = true; $('signin').click(); }
  else status('Sign-in did not finish. Choose Sign in with Google to try again.');
})();
