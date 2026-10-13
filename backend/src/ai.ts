import { readFileSync } from 'node:fs';
import { readSetup } from './setup.js';
import { z } from 'zod';
import { db, config, month, micros, accountRef, hash, expires, ApiError, type Account } from './core.js';

const instructions = readFileSync(new URL('../card-instructions.txt', import.meta.url), 'utf8');
const inputSchema = z.object({
  task: z.enum(['Create a new card','Revise this card']), prompt: z.string().min(1).max(4000),
  width: z.number().min(1).max(1920), height: z.number().min(1).max(1080),
  existing_name: z.string().max(100).optional(), existing_html: z.string().max(200000).optional(),
  existing_state: z.record(z.string(), z.unknown()).optional(),
  available_images: z.array(z.object({ id: z.string().max(100), description: z.string().max(2000), width: z.number(), height: z.number() }).strict()).max(16).optional(),
  website_source: z.object({ url: z.string().max(2048), text: z.string().max(24000), truncated: z.boolean(), style_sample: z.string().max(2400), notice: z.string().max(2000) }).strict().optional()
}).strict();
const cardFormat = { type: 'json_schema', name: 'callayers_card', strict: true,
  schema: { type: 'object', properties: { name: { type: 'string' }, html: { type: 'string' } }, required: ['name','html'], additionalProperties: false } };
const requestRef = (uid: string, id: string) => db.collection('requests').doc(uid + '_' + id);
const resultRef = (uid: string, id: string) => db.collection('results').doc(uid + '_' + id);
const globalRef = (period: string) => db.collection('globalUsage').doc(period);
const userMonthRef = (uid: string, period: string) => accountRef(uid).collection('months').doc(period);
const count = z.number().int().nonnegative().max(10000000);
const usageSchema = z.object({ input_tokens: count, output_tokens: count,
  input_tokens_details: z.object({ cached_tokens: count.default(0) }).passthrough().optional(),
  output_tokens_details: z.object({ reasoning_tokens: count.default(0) }).passthrough().optional() }).passthrough();

export async function accountStatus(uid: string) {
  const [a, m] = await Promise.all([accountRef(uid).get(), userMonthRef(uid, month()).get()]);
  const account = a.data() as Account;
  if (!account) throw new ApiError(404, 'account_not_found', 'Account not found. Sign in from the desktop first.');
  const spending = m.data() ?? { spent: 0, reserved: 0 };
  const limit = micros(account.paid ? config.PAID_MONTHLY_USD : config.TRIAL_USD);
  const used = account.paid ? spending.spent + spending.reserved : account.trialSpent + account.trialReserved;
  // Retain the response field for older clients; approval is no longer a permission.
  return { uid, email: account.email, approved: true, paid: account.paid, disabled: account.disabled, setup: readSetup(account.setup),
    trialStartedAt: account.trialStartedAt, trialEndsAt: account.trialEndsAt,
    remainingUsd: Math.max(0, limit - used) / 1000000, monthlySpentUsd: spending.spent / 1000000,
    aiConfigured: config.AI_ENABLED === 'true' && !!config.OPENAI_API_KEY && !!config.PRICE_VERSION && config.INPUT_USD_PER_MILLION !== undefined && config.OUTPUT_USD_PER_MILLION !== undefined };
}
export async function requestStatus(uid: string, id: string) {
  z.uuid().parse(id);
  const record = (await requestRef(uid, id).get()).data();
  if (!record) throw new ApiError(404, 'request_not_found', 'That request has not been received.');
  if (record.state === 'pending') return { state: 'pending', requestId: id };
  const result = (await resultRef(uid, id).get()).data();
  if (result && result.expiresAt.toMillis() > Date.now()) return { state: 'completed', requestId: id, response: result.response };
  return { state: record.state === 'completed' ? 'result_expired' : record.state, requestId: id,
    code: record.code ?? 'result_expired', message: record.message ?? 'The saved result expired. Start a new generation.' };
}
export async function generate(uid: string, body: unknown) {
  const { requestId, input } = z.object({ requestId: z.uuid(), input: inputSchema }).strict().parse(body);
  const serialized = JSON.stringify(input);
  if (Buffer.byteLength(serialized) > config.MAX_INPUT_BYTES) throw new ApiError(413, 'input_too_large', 'This card is too large for the AI service. Try a smaller card.');
  const ref = requestRef(uid, requestId); const fingerprint = hash(serialized);
  const prior = (await ref.get()).data();
  if (prior) {
    if (prior.fingerprint !== fingerprint) throw new ApiError(409, 'request_id_reused', 'Start a new request for the changed card.');
    return requestStatus(uid, requestId);
  }
  const inputRate = config.INPUT_USD_PER_MILLION, outputRate = config.OUTPUT_USD_PER_MILLION;
  if (config.AI_ENABLED !== 'true' || !config.OPENAI_API_KEY || inputRate === undefined || outputRate === undefined || !config.PRICE_VERSION)
    throw new ApiError(503, 'ai_unconfigured', 'AI is not enabled yet. Your local cards and camera still work.');
  // UTF-8 bytes provide a conservative text-token ceiling, plus framing/schema headroom.
  // Only bounded text, the fixed schema, and fixed instructions reach the provider. No tools/media.
  const reserved = Math.ceil((Buffer.byteLength(serialized) + Buffer.byteLength(instructions) + 4096) * Math.max(inputRate, config.CACHED_USD_PER_MILLION ?? inputRate) + config.MAX_OUTPUT_TOKENS * outputRate);
  const period = month(); const now = Date.now();
  const fresh = await db.runTransaction(async tx => {
    const [a, m, g, r, policy] = await Promise.all([tx.get(accountRef(uid)), tx.get(userMonthRef(uid, period)), tx.get(globalRef(period)), tx.get(ref), tx.get(db.doc('controls/ai'))]);
    if (r.exists) {
      if (r.data()!.fingerprint !== fingerprint) throw new ApiError(409, 'request_id_reused', 'Start a new request.');
      return false;
    }
    const account = a.data() as Account;
    if (!account) throw new ApiError(404, 'account_not_found', 'Sign in to Callayers to use AI.');
    if (account.disabled) throw new ApiError(403, 'account_disabled', 'This account is disabled. Contact Callayers support.');
    if (policy.data()?.disabled) throw new ApiError(503, 'ai_paused', 'AI generation is temporarily paused.');
    const trialStart = account.trialStartedAt ?? now;
    const trialEnd = account.trialEndsAt ?? trialStart + config.TRIAL_DAYS * 86400000;
    if (!account.paid && now >= trialEnd) throw new ApiError(403, 'trial_expired', 'Your trial has ended. Contact us for paid access. Existing cards and camera output still work.');
    if (account.activeUntil > now) throw new ApiError(429, 'generation_in_progress', 'A generation is already running for your account.');
    if (now - account.lastRequestAt < 5000) throw new ApiError(429, 'generation_rate_limited', 'Wait a few seconds before generating another card.');
    const monthly = m.data() ?? { spent: 0, reserved: 0 }; const global = g.data() ?? { spent: 0, reserved: 0 };
    const used = account.paid ? monthly.spent + monthly.reserved : account.trialSpent + account.trialReserved;
    if (used + reserved > micros(account.paid ? config.PAID_MONTHLY_USD : config.TRIAL_USD)) {
      const date = new Date(now);
      const reset = new Date(Date.UTC(date.getUTCFullYear(), date.getUTCMonth() + 1, 1)).toISOString().slice(0, 10);
      throw new ApiError(403, 'allowance_exhausted', account.paid
        ? `Your remaining AI allowance is too small for this request. Your monthly allowance resets on ${reset} (UTC).`
        : 'Your remaining AI trial allowance is too small for this request. Contact Callayers support for a larger allowance. Your existing cards and camera still work.');
    }
    if (global.spent + global.reserved + reserved > micros(config.GLOBAL_MONTHLY_USD)) throw new ApiError(503, 'global_allowance_exhausted', 'AI is temporarily unavailable because the service allowance has been reached. Please try again later.');
    tx.update(accountRef(uid), { ...(!account.paid ? { trialStartedAt: trialStart, trialEndsAt: trialEnd, trialReserved: account.trialReserved + reserved } : {}), activeRequest: requestId, activeUntil: now + 180000, lastRequestAt: now });
    tx.set(userMonthRef(uid, period), { ...monthly, reserved: monthly.reserved + reserved });
    tx.set(globalRef(period), { ...global, reserved: global.reserved + reserved });
    tx.create(ref, { uid, requestId, fingerprint, state: 'pending', createdAt: now, period, reserved,
      trial: !account.paid, model: config.AI_MODEL, priceVersion: config.PRICE_VERSION, inputRate, outputRate,
      cachedRate: config.CACHED_USD_PER_MILLION ?? inputRate, operation: input.task === 'Create a new card' ? 'create' : 'revise' });
    return true;
  });
  if (!fresh) return requestStatus(uid, requestId);
  let response: Record<string, unknown> | undefined;
  let billed = reserved, uncertain = true, code = 'provider_outcome_unknown';
  let message = 'The provider outcome could not be confirmed. Allowance has been held conservatively; contact support before retrying.';
  let usage: z.infer<typeof usageSchema> | undefined;
  try {
    const provider = await fetch('https://api.openai.com/v1/responses', {
      method: 'POST', headers: { Authorization: 'Bearer ' + config.OPENAI_API_KEY, 'Content-Type': 'application/json' },
      body: JSON.stringify({ model: config.AI_MODEL, instructions, input: serialized, text: { format: cardFormat },
        max_output_tokens: config.MAX_OUTPUT_TOKENS, store: false, service_tier: 'default' }), signal: AbortSignal.timeout(120000)
    });
    if (!provider.ok) {
      code = 'provider_http_' + provider.status;
      if ([400,401,403,404,422,429].includes(provider.status)) { billed = 0; uncertain = false; }
      message = provider.status === 429 ? 'The AI provider is busy or its allowance is exhausted. Try later.' : 'The AI provider could not complete this card. Contact support if this repeats.';
    } else {
      // Read with an explicit cap; do not persist unbounded output in Firestore.
      const reader = provider.body!.getReader(); let bytes = 0; const chunks: Uint8Array[] = [];
      for (;;) { const part = await reader.read(); if (part.done) break; bytes += part.value.length;
        if (bytes > 800000) { await reader.cancel(); throw new Error('oversized'); } chunks.push(part.value); }
      response = JSON.parse(Buffer.concat(chunks).toString()) as Record<string, unknown>;
      usage = usageSchema.parse(response.usage);
      const cached = usage.input_tokens_details?.cached_tokens ?? 0;
      if (cached > usage.input_tokens || response.service_tier !== undefined && response.service_tier !== 'default') throw new Error('unexpected usage');
      billed = Math.ceil((usage.input_tokens - cached) * inputRate + cached * (config.CACHED_USD_PER_MILLION ?? inputRate) + usage.output_tokens * outputRate);
      uncertain = false; code = response.status === 'completed' ? 'completed' : 'provider_incomplete';
      message = 'The AI provider did not finish the card. Try a simpler request.';
    }
  } catch { response = undefined; code = 'provider_outcome_unknown'; }
  // If persistence fails, the existing reservation remains counted. Never release on a timer.
  await db.runTransaction(async tx => {
    const [r, a, m, g] = await Promise.all([tx.get(ref), tx.get(accountRef(uid)), tx.get(userMonthRef(uid, period)), tx.get(globalRef(period))]);
    if (r.data()!.state !== 'pending') return;
    const account = a.data() as Account, monthly = m.data()!, global = g.data()!;
    tx.update(accountRef(uid), { totalSpent: account.totalSpent + billed,
      ...(r.data()!.trial ? { trialSpent: account.trialSpent + billed, trialReserved: account.trialReserved - reserved } : {}),
      ...(account.activeRequest === requestId ? { activeRequest: null, activeUntil: 0 } : {}) });
    tx.update(userMonthRef(uid, period), { spent: monthly.spent + billed, reserved: monthly.reserved - reserved });
    tx.update(globalRef(period), { spent: global.spent + billed, reserved: global.reserved - reserved });
    tx.update(ref, { state: response ? 'completed' : uncertain ? 'uncertain' : 'failed', code, message, estimatedCostMicros: billed,
      uncertain, completedAt: Date.now(), latencyMs: Date.now() - now, usage: usage ?? null });
    if (response) tx.set(resultRef(uid, requestId), { response, expiresAt: expires(86400000) });
    if (billed > reserved) tx.set(db.doc('controls/ai'), { disabled: true, reason: 'reservation_exceeded', requestId });
  });
  return requestStatus(uid, requestId);
}
