import { initializeApp } from 'firebase/app';
import { getAuth, GoogleAuthProvider, signInWithPopup, signOut, onAuthStateChanged, setPersistence, browserSessionPersistence } from 'firebase/auth';

const $ = (id: string) => document.getElementById(id)!;
const status = (text: string) => { $('status').textContent = text; };
const firebaseConfig = await fetch('/__/firebase/init.json').then(r => { if (!r.ok) throw new Error('Firebase Hosting configuration is unavailable.'); return r.json(); });
const auth = getAuth(initializeApp(firebaseConfig));
await setPersistence(auth, browserSessionPersistence);
async function api(path: string, method = 'GET', body?: unknown): Promise<any> {
  if (!auth.currentUser) throw new Error('Sign in first.');
  const token = await auth.currentUser.getIdToken();
  const response = await fetch('/v1/' + path, { method, headers: { Authorization: 'Bearer ' + token, 'Content-Type': 'application/json' }, body: body === undefined ? undefined : JSON.stringify(body) });
  const data = await response.json(); if (!response.ok) throw new Error(data.message ?? 'The request failed.'); return data;
}
const handle = (fn: () => Promise<void>) => async () => { try { await fn(); } catch (e) { status(e instanceof Error ? e.message : 'Something went wrong.'); } };
$('signin').onclick = handle(async () => { const provider = new GoogleAuthProvider(); provider.setCustomParameters({ prompt: 'select_account' }); await signInWithPopup(auth, provider); });
$('signout').onclick = handle(async () => { await signOut(auth); location.reload(); });
const loginId = location.hash.slice(1);
if (loginId) history.replaceState(null, '', location.pathname);
let selected = '', cursor: number | null = null;
const money = (micros: number) => '$' + ((micros ?? 0) / 1000000).toFixed(4);
function table(id: string, headings: string[], rows: Array<Array<string | HTMLElement>>) {
  const t = document.createElement('table'); const head = document.createElement('tr');
  for (const text of headings) { const cell = document.createElement('th'); cell.scope = 'col'; cell.textContent = text; head.append(cell); } t.append(head);
  for (const row of rows) { const tr = document.createElement('tr'); for (const item of row) { const td = document.createElement('td'); if (typeof item === 'string') td.textContent = item; else td.append(item); tr.append(td); } t.append(tr); }
  $(id).replaceChildren(t);
}
async function detail(uid: string) {
  selected = uid; const a = await api('admin/accounts/' + encodeURIComponent(uid));
  $('detail').hidden = false; $('account-heading').textContent = a.email;
  $('account-summary').textContent = `Remaining allowance: $${a.remainingUsd.toFixed(4)} · Trial ends: ${a.trialEndsAt ? new Date(a.trialEndsAt).toLocaleString() : 'not started'}`;
  for (const key of ['approved','paid','disabled']) ($ (key) as HTMLInputElement).checked = a[key];
  table('requests', ['Request','State','Cost','Input / output tokens','Created'], a.requests.map((r: any) => [r.requestId, r.state + (r.uncertain ? ' (estimated reservation)' : ''), money(r.estimatedCostMicros ?? r.reserved), r.usage ? `${r.usage.input_tokens} / ${r.usage.output_tokens}` : 'unknown', new Date(r.createdAt).toLocaleString()]));
}
function showAccounts(accounts: any[]) {
  table('accounts', ['Account','Access','Lifetime estimated AI cost','Details'], accounts.map(a => {
    const button = document.createElement('button'); button.textContent = 'Open'; button.onclick = handle(() => detail(a.uid));
    return [a.email, a.disabled ? 'Disabled' : a.paid ? 'Paid' : a.approved ? 'Trial approved' : 'Pending approval', money(a.totalSpent), button];
  }));
}
async function accounts(email = '', next = false) {
  const query = email ? '?email=' + encodeURIComponent(email) : next && cursor ? '?cursor=' + cursor : '';
  const result = await api('admin/accounts' + query); cursor = result.nextCursor; showAccounts(result.accounts); $('more').hidden = !cursor;
}
async function spending(top = false) {
  const s = await api('admin/spending');
  $('spending').textContent = `${s.period} UTC: ${money(s.global.spent)} spent + ${money(s.global.reserved)} reserved / $${s.globalLimitUsd} global limit. AI ${s.paused ? 'PAUSED' : 'not manually paused'}.`;
  if (top) { showAccounts(s.accounts); $('more').hidden = true; }
}
if ($('admin')) {
  $('search').onsubmit = e => { e.preventDefault(); void handle(() => accounts(($('email') as HTMLInputElement).value.trim()))(); };
  $('all').onclick = handle(() => accounts()); $('more').onclick = handle(() => accounts('', true)); $('top').onclick = handle(() => spending(true));
  $('access').onsubmit = e => { e.preventDefault(); void handle(async () => {
    const change = Object.fromEntries(['approved','paid','disabled'].map(k => [k, ($(k) as HTMLInputElement).checked]));
    await api('admin/accounts/' + encodeURIComponent(selected), 'PATCH', change); status('Access updated.'); await detail(selected); await accounts();
  })(); };
  for (const [id, disabled] of [['pause', true], ['resume', false]] as const) $(id).onclick = handle(async () => { await api('admin/ai','POST',{disabled}); await spending(); });
} else {
  $('approve').onclick = handle(async () => {
    if (($('confirm-code') as HTMLInputElement).value.trim().toUpperCase() !== loginId.slice(0,8).toUpperCase()) throw new Error('Type the matching code shown in Callayers.');
    await api('login/approve','POST',{id:loginId}); $('pairing').hidden = true; status('Connected. Return to Callayers. This tab can be closed.');
  });
}
onAuthStateChanged(auth, user => { void handle(async () => {
  $('signin').hidden = !!user; $('signout').hidden = !user;
  if (!user) { status('Sign in to continue.'); return; }
  const me = await api('browser/me'); status(`Signed in as ${me.email}\nAccount ID: ${me.uid}`);
  if ($('admin')) { if (!me.admin) throw new Error(`This account is not an administrator. Account ID: ${me.uid}`); $('admin').hidden = false; await accounts(); await spending(); }
  else if (/^[a-f0-9]{64}$/.test(loginId)) { $('pair-code').textContent = loginId.slice(0,8).toUpperCase(); $('pairing').hidden = false; }
  else status('Open File → Account → Sign in with Google in the Windows app to connect it.');
})(); });
