import test from 'node:test';
import assert from 'node:assert/strict';
import { telemetryBatch } from '../dist/telemetry-schema.js';
const base = { id: 'a564f1eb-89f9-4d64-9e36-1d131cff5070', session_id: 'session', install_id: 'installation', timestamp: '2026-10-12T21:00:00.000Z', app_version: '0.5.0' };
const props = { action: 'created', annotation_type: 'text', change_source: 'direct', count: 1, consuming: false };
const parse = properties => telemetryBatch.safeParse({ events: [{ ...base, event: 'annotation_changed', properties }] });
test('annotation metadata accepts supported actions, sources and types', () => {
  for (const action of ['created','edited','deleted','cleared'])
    for (const change_source of ['direct','undo','redo','card_switch'])
      for (const annotation_type of ['text','drawing'])
        assert.equal(parse({ ...props, action, change_source, annotation_type }).success, true);
});
test('annotation events reject content, missing fields, and malformed metadata', () => {
  for (const key of Object.keys(props)) { const p = { ...props }; delete p[key]; assert.equal(parse(p).success, false); }
  for (const extra of [{ text: 'private' }, { points: [[.1,.2]] }, { prompt: 'private' }, { card_id: 'id' }, { count: 0 }, { count: 1001 }, { count: 1.5 }, { consuming: 'true' }, { action: 'typed' }, { annotation_type: 'image' }])
    assert.equal(parse({ ...props, ...extra }).success, false);
});
test('existing events remain compatible in a mixed batch', () => {
  assert.equal(telemetryBatch.safeParse({ events: [
    { ...base, event: 'session_heartbeat', properties: { uptime_ms: 60000 } },
    { ...base, event: 'session_heartbeat', properties: { uptime_ms: 360000, visible_annotation_count: 2, preview_visible: true, consumer_count: 1, visible_card_count: 0 } },
    { ...base, event: 'annotation_changed', properties: props }
  ] }).success, true);
  assert.equal(telemetryBatch.safeParse({ events: [{ ...base, event: 'session_heartbeat', properties: props }] }).success, false);
});
