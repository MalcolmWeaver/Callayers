import { initializeApp } from 'firebase/app';
import { getAuth, GoogleAuthProvider, signInWithPopup, signOut, onAuthStateChanged, setPersistence, browserSessionPersistence } from 'firebase/auth';

const $ = (id: string) => document.getElementById(id)!;
const status = (text: string) => { $('status').textContent = text; };
const handle = (fn: () => Promise<void>) => async () => {
  try { await fn(); } catch (e) { status(e instanceof Error ? e.message : 'Something went wrong.'); }
};
const [loginId = '', mode] = location.hash.slice(1).split('&');
const automatic = mode === 'desktop=1';
if (loginId) history.replaceState(null, '', location.pathname);
void handle(async () => {
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
      await signInWithPopup(auth, provider);
      if (automatic) {
        status('Returning to Callayers…');
        const result = await api('login/approve', { id: loginId });
        const target = new URL(result.returnUrl);
        if (target.protocol !== 'http:' || target.hostname !== '127.0.0.1' || target.pathname !== '/callback' || target.username || target.password)
          throw new Error('Invalid desktop return address. Start sign-in again in Callayers.');
        location.replace(target.href);
      }
    } catch (e) {
      if ((e as { code?: string }).code === 'auth/popup-blocked') {
        status('Choose Sign in with Google to continue. Your browser blocked the automatic account picker.');
      } else throw e;
    } finally { button.disabled = false; }
  });
  $('signout').onclick = handle(async () => { await signOut(auth); });
  $('approve').onclick = handle(async () => {
    if (($('confirm-code') as HTMLInputElement).value.trim().toUpperCase() !== loginId.slice(0, 8).toUpperCase())
      throw new Error('Type the matching code shown in Callayers.');
    const button = $('approve') as HTMLButtonElement; button.disabled = true;
    try {
      await api('login/approve', { id: loginId }); $('pairing').hidden = true;
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
  if (automatic && /^[a-f0-9]{64}$/.test(loginId)) {
    status('Opening Google account chooser…');
    $('signin').click();
  }
})();
