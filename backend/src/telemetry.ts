import { config, ApiError, rateLimit } from './core.js';
import { telemetryBatch } from './telemetry-schema.js';
export async function capture(uid: string, body: unknown) {
  const { events } = telemetryBatch.parse(body);
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
