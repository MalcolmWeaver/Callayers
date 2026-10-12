import { z } from 'zod';

const identifier = z.string().max(100).regex(/^[a-zA-Z0-9{}_.:-]+$/);
const annotationProperties = z.object({
  action: z.enum(['created','edited','deleted','cleared']),
  annotation_type: z.enum(['text','drawing']),
  change_source: z.enum(['direct','undo','redo','card_switch']),
  count: z.number().int().min(1).max(1000), consuming: z.boolean()
}).strict();
export const telemetryEvent = z.object({
  id: z.uuid(), session_id: identifier, install_id: identifier,
  timestamp: z.iso.datetime(), app_version: z.string().max(40),
  event: z.enum(['process_state_changed','preview_state_changed','vcam_state_changed','session_heartbeat',
    'setup_completed','card_stage_changed','card_output_changed','card_generation','failure','recovery','annotation_changed']),
  properties: z.object({
    state: z.enum(['started','ended','visible','hidden','consuming','idle','shown','removed','succeeded','failed','canceled','discarded']).optional(),
    operation: z.enum(['create','revise']).optional(), component: identifier.optional(), code: identifier.optional(),
    native_code: z.number().int().optional(), consumer_count: z.number().int().min(0).max(1000).optional(),
    preview_visible: z.boolean().optional(),
    visible_card_count: z.number().int().min(0).max(1000).optional(),
    visible_annotation_count: z.number().int().min(0).max(1000).optional(),
    card_id: identifier.optional(), request_id: z.uuid().optional(), uptime_ms: z.number().nonnegative().optional(),
    ...annotationProperties.partial().shape
  }).strict()
}).strict().superRefine((event, ctx) => {
  if (event.event === 'annotation_changed') {
    if (!annotationProperties.safeParse(event.properties).success)
      ctx.addIssue({ code: 'custom', path: ['properties'], message: 'Invalid annotation action metadata.' });
  } else if (Object.keys(annotationProperties.shape).some(key => key in event.properties)) {
    ctx.addIssue({ code: 'custom', path: ['properties'], message: 'Annotation metadata requires an annotation event.' });
  }
});
export const telemetryBatch = z.object({ events: z.array(telemetryEvent).min(1).max(50) }).strict();
