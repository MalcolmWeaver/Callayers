import { createHash, randomBytes } from 'node:crypto';

export const WEBSITE_ORIGIN = 'https://callayers.com';
export const ticketId = ticket => `attribution_${createHash('sha256').update(ticket).digest('hex')}`;
export const newTicket = () => randomBytes(32).toString('base64url');

export async function readTicket(req) {
  if (!/^application\/json(?:\s*;.*)?$/i.test(req.headers['content-type'] || '')) return null;
  let body = '';
  for await (const chunk of req) {
    body += chunk.toString('utf8');
    if (body.length > 256) return null;
  }
  try {
    const value = JSON.parse(body);
    return Object.keys(value).length === 1 && /^[A-Za-z0-9_-]{43}$/.test(value.ticket) ? value.ticket : null;
  } catch { return null; }
}
