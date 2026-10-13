export type SignInFields = { email: string; password: string };
export type SignUpFields = SignInFields & { name: string; confirmPassword: string };
export type FieldErrors<T> = Partial<Record<keyof T, string>>;

const emailPattern = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

export function validateSignIn(fields: SignInFields): FieldErrors<SignInFields> {
  const errors: FieldErrors<SignInFields> = {};
  if (!fields.email.trim()) errors.email = 'Email is required.';
  else if (!emailPattern.test(fields.email.trim())) errors.email = 'Enter a valid email address.';
  if (!fields.password) errors.password = 'Password is required.';
  return errors;
}

export function validateSignUp(fields: SignUpFields): FieldErrors<SignUpFields> {
  const errors: FieldErrors<SignUpFields> = validateSignIn(fields);
  if (fields.password && fields.password.length < 8) errors.password = 'Password must be at least 8 characters.';
  if (!fields.name.trim()) errors.name = 'Name is required.';
  if (!fields.confirmPassword) errors.confirmPassword = 'Confirm your password.';
  else if (fields.confirmPassword !== fields.password) errors.confirmPassword = 'Passwords do not match.';
  return errors;
}
