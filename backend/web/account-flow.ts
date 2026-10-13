import { validateSignIn, validateSignUp, type FieldErrors, type SignInFields, type SignUpFields } from './validation.ts';

type Failure<T> = { ok: false; fields: FieldErrors<T>; formError?: string };
type Success = { ok: true };
export type SubmitResult<T> = Success | Failure<T>;

export async function submitSignIn(fields: SignInFields, action: (fields: SignInFields) => Promise<unknown>): Promise<SubmitResult<SignInFields>> {
  const errors = validateSignIn(fields);
  if (Object.keys(errors).length) return { ok: false, fields: errors };
  try { await action({ email: fields.email.trim(), password: fields.password }); return { ok: true }; }
  catch (error) {
    return { ok: false, fields: {}, formError: (error as { code?: string })?.code === 'INVALID_CREDENTIALS'
      ? 'Invalid email or password.' : 'Sign-in is unavailable. Please try again.' };
  }
}

export async function submitSignUp(fields: SignUpFields, action: (fields: Omit<SignUpFields, 'confirmPassword'>) => Promise<unknown>): Promise<SubmitResult<SignUpFields>> {
  const errors = validateSignUp(fields);
  if (Object.keys(errors).length) return { ok: false, fields: errors };
  try { await action({ name: fields.name.trim(), email: fields.email.trim(), password: fields.password }); return { ok: true }; }
  catch (error) {
    if ((error as { code?: string })?.code === 'EMAIL_TAKEN')
      return { ok: false, fields: { email: 'Email already registered.' } };
    return { ok: false, fields: {}, formError: 'Sign-up is unavailable. Please try again.' };
  }
}
