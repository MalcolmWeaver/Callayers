import type { FastifyInstance } from 'fastify';

// Firebase's documented same-origin auth helper proxy. Never forward app cookies
// or Authorization headers, and never accept a caller-selected upstream host.
export async function registerAuthProxy(app: FastifyInstance, project: string, request: typeof fetch = fetch) {
  if (!/^[a-z][a-z0-9-]{4,61}[a-z0-9]$/.test(project)) throw new Error('Invalid Firebase project ID');
  await app.register(async scope => {
    scope.removeAllContentTypeParsers();
    scope.addContentTypeParser('*', { parseAs: 'buffer' }, (_req, body, done) => done(null, body));
    scope.route<{ Params: { '*': string } }>({
      method: ['GET', 'POST'], url: '/__/auth/*',
      handler: async (req, reply) => {
        const path = req.params['*'];
        if (!/^[a-zA-Z0-9_-]+(?:\.js)?$/.test(path)) return reply.code(404).send();
        const query = req.raw.url?.split('?').slice(1).join('?');
        const upstream = `https://${project}.firebaseapp.com/__/auth/${path}${query ? '?' + query : ''}`;
        try {
          const result = await request(upstream, {
            method: req.method, redirect: 'error', signal: AbortSignal.timeout(15000),
            headers: req.headers['content-type'] ? { 'Content-Type': req.headers['content-type'] } : {},
            ...(req.method === 'POST' ? { body: req.body as Uint8Array<ArrayBuffer> } : {})
          });
          reply.header('Cache-Control', 'no-store').header('Referrer-Policy', 'no-referrer')
            .header('X-Content-Type-Options', 'nosniff');
          return reply.code(result.status).type(result.headers.get('content-type') ?? 'application/octet-stream')
            .send(Buffer.from(await result.arrayBuffer()));
        } catch {
          return reply.code(502).send({ message: 'Google sign-in is temporarily unavailable. Start sign-in again in Callayers.' });
        }
      }
    });
  });
}
