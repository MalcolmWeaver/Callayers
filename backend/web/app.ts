import { initializeApp } from 'firebase/app';
import { getAuth, GoogleAuthProvider, signInWithRedirect, getRedirectResult, signOut, onAuthStateChanged, setPersistence, browserSessionPersistence } from 'firebase/auth';

const $ = (id: string) => document.getElementById(id)!;
const status = (text: string) => { $('status').textContent = text; };
const handle = (fn: () => Promise<void>) => async () => {
  try { await fn(); } catch (e) {
    $('signin').hidden = false;
    status(e instanceof Error ? e.message : 'Something went wrong.');
  }
};
void handle(async () => {
  // Tab-scoped state survives Google's redirect; no desktop credential is stored here.
  const storageKey = 'callayers.google-redirect';
  const incoming = location.hash.slice(1);
  let pending: { id: string; automatic: boolean; started: number; redirecting: boolean } | undefined;
  if (incoming) {
    sessionStorage.removeItem(storageKey);
    const [id, mode] = incoming.split('&');
    history.replaceState(null, '', location.pathname);
    if (!id || !/^[a-f0-9]{64}$/.test(id) || (mode && mode !== 'desktop=1'))
      throw new Error('Start sign-in again in Callayers.');
    pending = { id, automatic: mode === 'desktop=1', started: Date.now(), redirecting: false };
    sessionStorage.setItem(storageKey, JSON.stringify(pending));
  } else {
    try { pending = JSON.parse(sessionStorage.getItem(storageKey) ?? 'null') ?? undefined; } catch { sessionStorage.removeItem(storageKey); }
  }
  if (pending && (!/^[a-f0-9]{64}$/.test(pending.id) || typeof pending.automatic !== 'boolean' ||
      !Number.isFinite(pending.started) || Date.now() - pending.started > 600000 || pending.started > Date.now())) {
    sessionStorage.removeItem(storageKey);
    throw new Error('This connection expired. Start sign-in again in Callayers.');
  }
  const loginId = pending?.id ?? '';
  const automatic = pending?.automatic ?? false;
  const response = await fetch('/v1/client-config');
  if (!response.ok) throw new Error('Sign-in configuration is not complete yet.');
  const auth = getAuth(initializeApp(await response.json()));
  await setPersistence(auth, browserSessionPersistence);
  async function api(path: string, body?: unknown): Promise<any> {
    if (!auth.currentUser) throw new Error('Sign in first.');
    const response = await fetch('/v1/' + path, {
      method: body === undefined ? 'GET' : 'POST',
      headers: { Authorization: 'Bearer ' + await auth.currentUser.getIdToken(), 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body)
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.message ?? 'The request failed.');
    return data;
  }
  $('signin').onclick = handle(async () => {
    const provider = new GoogleAuthProvider(); provider.setCustomParameters({ prompt: 'select_account' });
    const button = $('signin') as HTMLButtonElement; button.disabled = true;
    try {
      if (pending) sessionStorage.setItem(storageKey, JSON.stringify({ ...pending, redirecting: true }));
      status('Opening Google account chooser…');
      await signInWithRedirect(auth, provider);
    } catch (e) {
      $('signin').hidden = false;
      throw e;
    } finally { button.disabled = false; }
  });
  $('signout').onclick = handle(async () => { await signOut(auth); });
  // Never approve using a remembered account alone, or a result from an older link.
  const credential = await getRedirectResult(auth);
  if (!incoming && pending?.redirecting && credential && automatic) {
    status('Returning to Callayers…');
    $('signin').hidden = true;
    const result = await api('login/approve', { id: loginId });
    const target = new URL(result.returnUrl);
    if (target.protocol !== 'http:' || target.hostname !== '127.0.0.1' || target.pathname !== '/callback' || target.username || target.password)
      throw new Error('Invalid desktop return address. Start sign-in again in Callayers.');
    sessionStorage.removeItem(storageKey);
    location.replace(target.href);
    return;
  }
  $('approve').onclick = handle(async () => {
    if (($('confirm-code') as HTMLInputElement).value.trim().toUpperCase() !== loginId.slice(0, 8).toUpperCase())
      throw new Error('Type the matching code shown in Callayers.');
    const button = $('approve') as HTMLButtonElement; button.disabled = true;
    try {
      await api('login/approve', { id: loginId }); sessionStorage.removeItem(storageKey); $('pairing').hidden = true;
      status('Connected. Return to Callayers. This tab can be closed.');
    } finally { button.disabled = false; }
  });
  onAuthStateChanged(auth, user => { void handle(async () => {
    // A remembered browser account never approves a new desktop connection by
    // itself. Only completion of this attempt's Google account picker does.
    if (automatic) return;
    $('signin').hidden = !!user; $('signout').hidden = !user; $('pairing').hidden = true;
    if (!user) { status('Sign in to continue.'); return; }
    const me = await api('browser/me'); status(`Signed in as ${me.email}`);
    if (/^[a-f0-9]{64}$/.test(loginId)) {
      $('pair-code').textContent = loginId.slice(0, 8).toUpperCase(); $('pairing').hidden = false;
    } else status('Open File → Account → Sign in with Google in the Windows app to connect it.');
  })(); });
  if (automatic && incoming) {
    $('signin').hidden = true;
    status('Opening Google account chooser…');
    $('signin').click();
  } else if (automatic) {
    status('Sign-in did not finish. Choose Sign in with Google to try again.');
  }
})();
