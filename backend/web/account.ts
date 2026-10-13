import { finishGoogleSignIn, getCurrentUser, onAuthChange, signIn, signInWithGoogle, signOut, signUp, type AccountUser } from './auth-service';
import { submitSignIn, submitSignUp, type SubmitResult } from './account-flow';
import { validateSignIn, validateSignUp, type SignInFields, type SignUpFields } from './validation';

const page = document.body.dataset.page;
const byId = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const status = byId<HTMLElement>('status');

function showUser(user: AccountUser) {
  byId<HTMLElement>('user-name').textContent = user.name;
  byId<HTMLElement>('user-email').textContent = user.email;
}

async function initializeHome() {
  try {
    await onAuthChange(user => {
      if (user) { showUser(user); status.textContent = ''; }
      else location.replace('/signin');
    });
    byId<HTMLButtonElement>('signout').addEventListener('click', async () => {
      const button = byId<HTMLButtonElement>('signout');
      button.disabled = true;
      status.textContent = 'Signing out…';
      try { await signOut(); location.replace('/signin'); }
      catch { status.textContent = 'Could not sign out. Please try again.'; button.disabled = false; }
    });
  } catch { status.textContent = 'Could not load your account. Please try again.'; }
}

async function initializeLanding() {
  const guestNav = byId<HTMLElement>('guest-nav');
  const accountNav = byId<HTMLElement>('account-nav');
  try {
    await onAuthChange(user => {
      guestNav.hidden = !!user;
      accountNav.hidden = !user;
      if (user) byId<HTMLElement>('user-name').textContent = user.name;
    });
    byId<HTMLButtonElement>('account-signout').addEventListener('click', async () => {
      const button = byId<HTMLButtonElement>('account-signout');
      button.disabled = true;
      try { await signOut(); }
      catch { status.textContent = 'Could not sign out. Please try again.'; }
      finally { button.disabled = false; }
    });
  } catch { guestNav.hidden = false; accountNav.hidden = true; }
}

function showResult<T extends SignInFields | SignUpFields>(result: SubmitResult<T>, form: HTMLFormElement) {
  for (const input of form.querySelectorAll<HTMLInputElement>('input')) {
    const message = result.ok ? '' : (result.fields as Record<string, string>)[input.name] ?? '';
    input.setAttribute('aria-invalid', message ? 'true' : 'false');
    byId<HTMLElement>(`${input.name}-error`).textContent = message;
  }
  status.textContent = result.ok ? '' : result.formError ?? '';
}

function setBusy(form: HTMLFormElement, busy: boolean, label: string) {
  for (const control of form.querySelectorAll<HTMLInputElement | HTMLButtonElement>('input, button')) control.disabled = busy;
  byId<HTMLButtonElement>('google-signin').disabled = busy;
  const button = form.querySelector<HTMLButtonElement>('button[type=submit]')!;
  button.textContent = busy ? `${label}…` : label;
}

async function initializeForm() {
  const form = byId<HTMLFormElement>('auth-form');
  const googleButton = byId<HTMLButtonElement>('google-signin');
  const label = page === 'signup' ? 'Create account' : 'Sign in';
  googleButton.addEventListener('click', async () => {
    googleButton.disabled = true;
    status.textContent = '';
    try { await signInWithGoogle(); }
    catch { status.textContent = 'Google sign-in did not start. Please try again.'; googleButton.disabled = false; }
  });
  try { await finishGoogleSignIn(); }
  catch { status.textContent = 'Google sign-in did not finish. Please try again.'; }
  try { if (await getCurrentUser()) { location.replace('/home'); return; } }
  catch { if (!status.textContent) status.textContent = 'Sign-in configuration is unavailable. Please try again later.'; }
  form.addEventListener('submit', async event => {
    event.preventDefault();
    if (page === 'signup') {
      const fields: SignUpFields = {
        name: byId<HTMLInputElement>('name').value,
        email: byId<HTMLInputElement>('email').value,
        password: byId<HTMLInputElement>('password').value,
        confirmPassword: byId<HTMLInputElement>('confirmPassword').value
      };
      // Validate before disabling controls or contacting Firebase.
      const errors = validateSignUp(fields);
      if (Object.keys(errors).length) { showResult({ ok: false, fields: errors }, form); return; }
      setBusy(form, true, label);
      const result = await submitSignUp(fields, signUp);
      showResult(result, form);
      if (result.ok) location.assign('/home');
    } else {
      const fields: SignInFields = { email: byId<HTMLInputElement>('email').value, password: byId<HTMLInputElement>('password').value };
      const errors = validateSignIn(fields);
      if (Object.keys(errors).length) { showResult({ ok: false, fields: errors }, form); return; }
      setBusy(form, true, label);
      const result = await submitSignIn(fields, signIn);
      showResult(result, form);
      if (result.ok) location.assign('/home');
    }
    setBusy(form, false, label);
  });
}

if (page === 'home') void initializeHome();
else if (page === 'signin' || page === 'signup') void initializeForm();
else if (page === 'landing') void initializeLanding();
