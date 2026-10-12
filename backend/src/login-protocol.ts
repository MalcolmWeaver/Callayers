import { z } from 'zod';

export const loginSecret = z.string().regex(/^[a-f0-9]{64}$/);
// No arbitrary redirect destinations, hostnames, credentials, or URL parameters.
export const desktopReturn = z.object({
  port: z.number().int().min(1024).max(65535), state: loginSecret
}).strict();
export const startLoginBody = z.object({ challenge: loginSecret, desktopReturn: desktopReturn.optional() }).strict();
export const exchangeLoginBody = z.object({ id: loginSecret, proof: loginSecret, grant: loginSecret.optional() }).strict();
export function desktopReturnUrl(target: z.infer<typeof desktopReturn>, grant: string) {
  const { port, state } = desktopReturn.parse(target);
  return `http://127.0.0.1:${port}/callback?state=${state}&grant=${loginSecret.parse(grant)}`;
}
