export interface DesktopTarget { port: number; state: string; started: number; redirecting: boolean }

export function validateTarget(value: unknown): DesktopTarget {
  const target = value as DesktopTarget | null;
  if (!target || !Number.isInteger(target.port) || target.port < 1024 || target.port > 65535 ||
      typeof target.state !== 'string' || !/^[a-f0-9]{64}$/.test(target.state) ||
      !Number.isFinite(target.started) || Date.now() - target.started > 600000 || target.started > Date.now() ||
      typeof target.redirecting !== 'boolean') throw new Error('This connection expired. Start sign-in again in Callayers.');
  return target;
}

export function desktopAction(target: DesktopTarget): string {
  validateTarget(target);
  return `http://127.0.0.1:${target.port}/callback?state=${target.state}`;
}
