#!/usr/bin/env python3
"""Bounded Mac checks with optional live-webcam soak testing; no webcam footage is saved."""
import argparse
import datetime
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=60, help='video duration per cycle (5–86400), plus recovery checks')
    parser.add_argument('--webcam', action='store_true', help='also test the webcam selected in Callayers; publishes live video')
    parser.add_argument('--hours', type=float, help='repeat camera cycles for up to this many hours (0.05–24)')
    parser.add_argument('--app', type=Path)
    args = parser.parse_args()
    if not 5 <= args.seconds <= 86400:
        parser.error('--seconds must be between 5 and 86400')
    if args.hours is not None and (not math.isfinite(args.hours) or not 0.05 <= args.hours <= 24):
        parser.error('--hours must be between 0.05 and 24')
    build = ROOT / 'build' / ('macos-camera-' + platform.machine())
    app = (args.app or build / 'Build/Products/Debug/Callayers Renderer Preview.app').resolve()
    executable = app / 'Contents/MacOS/Callayers Renderer Preview'
    if not executable.exists():
        parser.error('Build the signed Mac app first.')
    # Keep two runners from competing for the same physical camera and producer.
    run_lock = (build / 'unattended.lock').open('w')
    try:
        fcntl.flock(run_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        parser.error('Another unattended suite is already running.')
    processes = subprocess.check_output(['ps', '-axo', 'command='], text=True)
    if any('/Contents/MacOS/Callayers Renderer Preview' in line for line in processes.splitlines()):
        parser.error('Quit Callayers before testing. Close calling apps; test video is published to the virtual camera.')
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    run_id = str(uuid.uuid4())
    result_dir = build / 'unattended' / run_id
    result_dir.mkdir(parents=True)
    summary_path = result_dir / 'summary.json'
    summary = {'runID': run_id, 'started': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'app': str(app), 'passed': False, 'status': 'running', 'checks': [], 'cycles': [],
               'webcamRequested': args.webcam, 'hoursRequested': args.hours}
    binary = app / 'Contents/MacOS/Callayers Renderer Preview.debug.dylib'
    summary['binarySHA256'] = hashlib.sha256((binary if binary.exists() else executable).read_bytes()).hexdigest()

    def save():
        temporary = summary_path.with_suffix('.tmp')
        temporary.write_text(json.dumps(summary, indent=2) + '\n')
        temporary.replace(summary_path)

    def camera_check(label, seconds, webcam):
        phase_id = str(uuid.uuid4())
        report_path = result_dir / (label + '.json')
        summary['activePhase'] = label
        save()
        print('Testing ' + label + '…', flush=True)
        subprocess.run(['open', '-n', '-g', '--stdout', str(result_dir / (label + '.log')),
                        '--stderr', str(result_dir / (label + '-error.log')), str(app), '--args',
                        '--check-webcam-transport' if webcam else '--check-camera-transport',
                        str(seconds), str(report_path), phase_id, str(os.getpid())], check=True, timeout=15)
        deadline = time.monotonic() + seconds + 60
        next_progress = time.monotonic() + 30
        while not report_path.exists():
            now = time.monotonic()
            if now >= deadline:
                raise RuntimeError(label + ' timed out; no pass was recorded')
            if now >= next_progress:
                summary['heartbeat'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
                save()
                print(label + ' running; Ctrl-C stops the suite.', flush=True)
                next_progress = now + 30
            time.sleep(0.5)
        report = json.loads(report_path.read_text())
        if report.get('runID') != phase_id:
            raise RuntimeError('Camera report belongs to another run')
        summary['cycles'].append({'name': label, 'report': report})
        save()
        for check in report.get('checks', []):
            print(('PASS ' if check['passed'] else 'FAIL ') + check['name'], flush=True)
        if not report.get('passed'):
            raise RuntimeError(report.get('error', label + ' failed'))
        # Report is written immediately before exit. Allow process teardown before relaunch.
        time.sleep(1)

    save()
    print('Results: ' + str(summary_path), flush=True)
    if args.webcam:
        print('Live webcam testing enabled. Keep calling apps closed. No webcam footage is saved.', flush=True)
    # Process-scoped idle-sleep prevention; display sleep is allowed.
    awake = subprocess.Popen(['/usr/bin/caffeinate', '-i', '-w', str(os.getpid())])
    exit_code = 1

    def interrupt(_signum, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupt)
    try:
        for name in ('WorkspaceChecks', 'CameraChecks', 'VideoChecks', 'ImageImportChecks', 'AnnotationChecks', 'AuthChecks', 'GenerationChecks', 'TelemetryChecks', 'RendererChecks'):
            command = [str(build / name)]
            if name == 'AuthChecks': command += ['--loopback']
            if name == 'CameraChecks': command += [str(result_dir / 'test-picture.png')]
            if name == 'ImageImportChecks': command += [str(result_dir / 'imported-card.html')]
            if name == 'RendererChecks':
                command = [sys.executable, str(ROOT / 'macos/Tests/run-renderer-checks.py'),
                           str(app.parent / name), str(app / 'Contents/Resources'), str(result_dir / 'renderer.png'), str(result_dir / 'imported-card.html')]
            with (result_dir / (name + '.log')).open('w') as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            summary['checks'].append({'name': name, 'passed': result.returncode == 0})
            save()
            print(('PASS ' if result.returncode == 0 else 'FAIL ') + name, flush=True)
            if result.returncode: raise RuntimeError(name + ' failed')
        budget_end = time.monotonic() + args.hours * 3600 if args.hours else None
        if args.webcam:
            camera_check('synthetic-preflight', 10, False)
        cycle = 0
        while True:
            seconds = args.seconds
            if budget_end is not None:
                seconds = min(seconds, int(budget_end - time.monotonic() - 60))
                if seconds < 5: break
            cycle += 1
            camera_check(('webcam-' if args.webcam else 'synthetic-') + str(cycle), seconds, args.webcam)
            if budget_end is None: break
        if not cycle: raise RuntimeError('No camera cycle completed within the time budget')
        summary['passed'] = True
        summary['status'] = 'passed'
        exit_code = 0
        print('PASS unattended suite', flush=True)
    except KeyboardInterrupt:
        summary['status'] = 'interrupted'
        summary['error'] = 'Stopped by user; this run is not a pass'
        exit_code = 130
        print('Stopped. The isolated camera test exits when this runner exits.', flush=True)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        summary['status'] = 'failed'
        summary['error'] = str(error)
        print('FAIL: ' + str(error), file=sys.stderr)
    finally:
        awake.terminate()
        awake.wait(timeout=5)
        summary.pop('activePhase', None)
        summary['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save()
        print('Results: ' + str(summary_path), flush=True)
    return exit_code


if __name__ == '__main__':
    sys.exit(main())
