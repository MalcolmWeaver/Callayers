import { z } from 'zod';
import { accountRef, ApiError, db } from './core.js';

export const setupSchema = z.object({
  version: z.literal(1), completed: z.boolean(), step: z.number().int().min(0).max(2),
  name: z.string().max(60), detail: z.string().max(80),
  cardId: z.string().max(128), app: z.number().int().min(0).max(3)
}).strict();
export const emptySetup = () => ({ version: 1 as const, completed: false, step: 0, name: '', detail: '', cardId: '', app: 0 });
export function readSetup(value: unknown) {
  const parsed = setupSchema.safeParse(value);
  return parsed.success ? parsed.data : emptySetup();
}
export async function saveSetup(uid: string, body: unknown) {
  const incoming = setupSchema.parse(body);
  return db.runTransaction(async tx => {
    const ref = accountRef(uid), snapshot = await tx.get(ref);
    if (!snapshot.exists) throw new ApiError(404, 'account_not_found', 'Sign in to save setup.');
    if (snapshot.data()!.disabled) throw new ApiError(403, 'account_disabled', 'This account is disabled.');
    // A delayed progress save on another computer must not reopen completed setup.
    const current = readSetup(snapshot.data()!.setup);
    const setup = current.completed ? current : incoming;
    tx.update(ref, { setup });
    return { setup };
  });
}
