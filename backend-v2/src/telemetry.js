const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
const cardId = /^[A-Za-z0-9_-]{1,64}$/;
const apps = new Set(['none','unknown','brave','chrome','edge','firefox','zoom','teams','discord','skype','obs','opera','windows_camera']);
function invalid(status = 400) { throw Object.assign(new Error('invalid_events'), { status }); }
function object(value) { return value !== null && typeof value === 'object' && !Array.isArray(value); }
function exact(value, keys) { if (!object(value) || Object.keys(value).length !== keys.length || keys.some(k => !Object.hasOwn(value,k))) invalid(); }
export function validateEvents(body, now = new Date()) {
  exact(body, ['events']);
  if (!Array.isArray(body.events) || body.events.length < 1 || body.events.length > 32) invalid();
  const seen = new Set();
  return body.events.map(e => {
    exact(e, ['id','sessionId','timestamp','appVersion','event','properties']);
    if (!uuid.test(e.id) || !uuid.test(e.sessionId) || seen.has(e.id) ||
        typeof e.appVersion !== 'string' || !/^[A-Za-z0-9_.-]{1,64}$/.test(e.appVersion) ||
        typeof e.timestamp !== 'string' || !/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z$/.test(e.timestamp)) invalid();
    const time = Date.parse(e.timestamp);
    if (!Number.isFinite(time) || new Date(time).toISOString() !== e.timestamp || time < now.getTime()-86400000 || time > now.getTime()+600000) invalid();
    seen.add(e.id);
    const p = e.properties;
    switch(e.event) {
    case 'session_started': exact(p,[]); break;
    case 'video_state_changed':
      exact(p,['preview_visible','consumer_connected','output_active','consumer_app']);
      if (typeof p.preview_visible !== 'boolean' || typeof p.consumer_connected !== 'boolean' || typeof p.output_active !== 'boolean' ||
          !apps.has(p.consumer_app) || (p.output_active && !p.consumer_connected) || (p.consumer_connected === (p.consumer_app === 'none'))) invalid();
      break;
    case 'content_created':
      if (p?.kind === 'annotation') exact(p,['kind']);
      else { exact(p,['kind','card_id']); if (!['ai_create','ai_revision','image_import'].includes(p.kind) || typeof p.card_id !== 'string' || !cardId.test(p.card_id)) invalid(); }
      break;
    case 'card_usage_changed':
      exact(p,['card_id','card_type','destination','applied']);
      if (typeof p.card_id !== 'string' || !cardId.test(p.card_id) || !['ai','image'].includes(p.card_type) ||
          !['preview','output'].includes(p.destination) || typeof p.applied !== 'boolean') invalid();
      break;
    default: invalid();
    }
    // Rebuild from approved fields; never forward headers, tokens or request bodies.
    return { id:e.id, sessionId:e.sessionId, timestamp:e.timestamp, appVersion:e.appVersion, event:e.event, properties:{...p} };
  });
}
export async function readEvents(req, now) {
  if (!/^application\/json(?:\s*;.*)?$/i.test(req.headers['content-type'] || '')) invalid(415);
  let size = 0; const chunks = [];
  for await (const chunk of req) { size += chunk.length; if (size > 32768) invalid(413); chunks.push(chunk); }
  let body; try { body = JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch { invalid(); }
  return validateEvents(body, now);
}
export function telemetryProvider(config, fetcher = fetch) {
  return async (uid, events) => {
    const batch = events.map(e => ({ uuid:e.id, event:e.event, distinct_id:`callayers:${uid}`, timestamp:e.timestamp,
      properties:{ ...e.properties, session_id:e.sessionId, app_version:e.appVersion, source:'callayers_desktop',
        $geoip_disable:true, $process_person_profile:false } }));
    // A retry preserves UUID, timestamp, event and owner for eventual deduplication.
    const response = await fetcher(`${config.posthogHost}/batch/`, { method:'POST', redirect:'error',
      headers:{'Content-Type':'application/json'}, body:JSON.stringify({api_key:config.posthogToken,batch}), signal:AbortSignal.timeout(4000) });
    await response.body?.cancel();
    if (!response.ok) throw new Error('telemetry_delivery_failed');
  };
}
// Bounded per-instance abuse protection, independent of AI allowance and Firestore.
export function telemetryLimiter() {
  const windows = new Map();
  return (uid, count, time) => {
    for (const [key, value] of windows) if (time-value.start >= 60000) windows.delete(key);
    let value=windows.get(uid);
    if (!value) { if (windows.size >= 1024) return false; value={start:time,count:0}; windows.set(uid,value); }
    if (value.count+count > 600) return false; value.count+=count; return true;
  };
}
