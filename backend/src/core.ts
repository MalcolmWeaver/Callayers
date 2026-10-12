import { initializeApp, applicationDefault } from 'firebase-admin/app';
import { getFirestore, Timestamp } from 'firebase-admin/firestore';
import { getAuth } from 'firebase-admin/auth';
import { createHash, randomBytes } from 'node:crypto';
import { z } from 'zod';

const positive = (fallback: number) => z.coerce.number().finite().positive().default(fallback);
const env = z.object({
  GOOGLE_CLOUD_PROJECT: z.string().min(1),
  PUBLIC_ORIGIN: z.url().refine(v => v.startsWith('https://') && new URL(v).pathname === '/').optional(),
  FIREBASE_WEB_API_KEY: z.string().min(1).optional(),
  TRIAL_DAYS: positive(7), TRIAL_USD: positive(1), PAID_MONTHLY_USD: positive(10), GLOBAL_MONTHLY_USD: positive(25),
  MAX_INPUT_BYTES: z.coerce.number().int().min(1024).max(250000).default(120000),
  MAX_OUTPUT_TOKENS: z.coerce.number().int().min(100).max(16000).default(12000),
  AI_MODEL: z.string().default('gpt-6-luna'), AI_ENABLED: z.enum(['true', 'false']).default('false'),
  OPENAI_API_KEY: z.string().optional(),
  INPUT_USD_PER_MILLION: z.coerce.number().positive().optional(),
  CACHED_USD_PER_MILLION: z.coerce.number().nonnegative().optional(),
  OUTPUT_USD_PER_MILLION: z.coerce.number().positive().optional(),
  PRICE_VERSION: z.string().optional(),
  POSTHOG_KEY: z.string().optional(),
  POSTHOG_HOST: z.enum(['https://us.i.posthog.com', 'https://eu.i.posthog.com']).default('https://us.i.posthog.com'),
  PORT: z.coerce.number().default(8080)
}).parse(process.env);
export const config = { ...env, PUBLIC_ORIGIN: env.PUBLIC_ORIGIN ? new URL(env.PUBLIC_ORIGIN).origin : undefined };
initializeApp({ credential: applicationDefault(), projectId: config.GOOGLE_CLOUD_PROJECT });
export const db = getFirestore();
export const auth = getAuth();
export const hash = (s: string) => createHash('sha256').update(s).digest('hex');
export const secret = () => randomBytes(32).toString('hex');
export const expires = (ms: number) => Timestamp.fromMillis(Date.now() + ms);
export const month = () => new Date().toISOString().slice(0, 7);
export const micros = (usd: number) => Math.round(usd * 1_000_000);
export class ApiError extends Error {
  constructor(public statusCode: number, public code: string, message: string) { super(message); }
}
export interface Account {
  email: string; approved: boolean; paid: boolean; disabled: boolean; createdAt: number;
  trialStartedAt: number | null; trialEndsAt: number | null;
  trialSpent: number; trialReserved: number; totalSpent: number;
  activeRequest: string | null; activeUntil: number; lastRequestAt: number;
}
export const newAccount = (email: string): Account => ({
  email, approved: false, paid: false, disabled: false, createdAt: Date.now(),
  trialStartedAt: null, trialEndsAt: null, trialSpent: 0, trialReserved: 0,
  totalSpent: 0, activeRequest: null, activeUntil: 0, lastRequestAt: 0
});
export const accountRef = (uid: string) => db.collection('accounts').doc(uid);
export async function rateLimit(key: string, limit: number, windowMs: number) {
  const ref = db.collection('limits').doc(hash(key));
  await db.runTransaction(async tx => {
    const old = (await tx.get(ref)).data(); const now = Date.now();
    const count = old && old.until > now ? old.count + 1 : 1;
    if (count > limit) throw new ApiError(429, 'rate_limited', 'Please wait a little and try again.');
    const until = old && old.until > now ? old.until : now + windowMs;
    tx.set(ref, { count, until, expiresAt: Timestamp.fromMillis(until) });
  });
}
