import { z } from 'zod';
import { config, ApiError, rateLimit } from './core.js';

const identifier = z.string().max(100).regex(/^[a-zA-Z0-9{}_.:-]+$/);
const event = z.object({
  id: z.uuid(), session_id: identifier, install_id: identifier,
  timestamp: z.iso.datetime(), app_version: z.string().max(40),
  event: z.enum(['process_state_changed','preview_state_changed','vcam_state_changed','session_heartbeat',
    'setup_completed','card_stage_changed','card_output_changed','card_generation','failure','recovery']),
  properties: z.object({
    state: z.enum(['started','ended','visible','hidden','consuming','idle','shown','removed','succeeded','failed','canceled','discarded']).optional(),
    operation: z.enum(['create','revise']).optional(), component: identifier.optional(), code: identifier.optional(),
    native_code: z.number().int().optional(), consumer_count: z.number().int().min(0).max(1000).optional(),
    preview_visible: z.boolean().optional(),
    visible_card_count: z.number().int().min(0).max(1000).optional(),
    visible_annotation_count: z.number().int().min(0).max(1000).optional(),
    card_id: identifier.optional(), request_id: z.uuid().optional(), uptime_ms: z.number().nonnegative().optional()
  }).strict()
}).strict();
export async function capture(uid: string, body: unknown) {
  const { events } = z.object({ events: z.array(event).min(1).max(50) }).strict().parse(body);
  await rateLimit('events:' + uid, 12, 60000);
  if (!config.POSTHOG_KEY) throw new ApiError(503, 'analytics_unconfigured', 'Analytics is not configured.');
  const batch = events.filter(e => Math.abs(Date.now() - Date.parse(e.timestamp)) < 7 * 86400000).map(e => ({
    uuid: e.id, event: e.event, timestamp: e.timestamp,
    properties: { ...e.properties, distinct_id: uid, $session_id: e.session_id, install_id: e.install_id,
      app_version: e.app_version, source: 'desktop', $geoip_disable: true, $process_person_profile: false }
  }));
  if (!batch.length) return { accepted: events.length };
  // Await receipt so the desktop can retry with identical UUIDs after failure.
  const response = await fetch(config.POSTHOG_HOST + '/batch/', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ api_key: config.POSTHOG_KEY, batch }), signal: AbortSignal.timeout(10000)
  });
  if (!response.ok) throw new ApiError(503, 'analytics_unavailable', 'Analytics is temporarily unavailable.');
  return { accepted: events.length };
}
