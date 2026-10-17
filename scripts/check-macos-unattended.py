#!/usr/bin/env python3
"""Bounded, webcam-free checks using the signed app and installed camera extension."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=60, help='sustained video duration (5–86400), plus recovery checks')
    parser.add_argument('--app', type=Path)
    args = parser.parse_args()
    if not 5 <= args.seconds <= 86400:
        parser.error('--seconds must be between 5 and 86400')
    build = ROOT / 'build' / ('macos-camera-' + platform.machine())
    app = (args.app or build / 'Build/Products/Debug/Callayers Renderer Preview.app').resolve()
    executable = app / 'Contents/MacOS/Callayers Renderer Preview'
    if not executable.exists():
        parser.error('Build the signed Mac app first.')
    processes = subprocess.check_output(['ps', '-axo', 'command='], text=True)
    if any('/Contents/MacOS/Callayers Renderer Preview' in line for line in processes.splitlines()):
        parser.error('Quit Callayers before testing. Also close calling apps; synthetic video will be published.')
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
    run_id = str(uuid.uuid4())
    result_dir = build / 'unattended' / run_id
    result_dir.mkdir(parents=True)
    report_path = result_dir / 'camera.json'
    summary = {'runID': run_id, 'started': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'app': str(app), 'passed': False, 'checks': []}
    binary = app / 'Contents/MacOS/Callayers Renderer Preview.debug.dylib'
    summary['binarySHA256'] = hashlib.sha256((binary if binary.exists() else executable).read_bytes()).hexdigest()
    # An idle-sleep assertion exists only while this process is alive; display sleep is allowed.
    awake = subprocess.Popen(['/usr/bin/caffeinate', '-i', '-w', str(os.getpid())])
    try:
        for name in ('WorkspaceChecks', 'CameraChecks', 'VideoChecks', 'RendererChecks'):
            command = [str(build / name)]
            if name == 'CameraChecks': command += [str(result_dir / 'test-picture.png')]
            if name == 'RendererChecks':
                command = [sys.executable, str(ROOT / 'macos/Tests/run-renderer-checks.py'),
                           str(app.parent / name), str(app / 'Contents/Resources'), str(result_dir / 'renderer.png')]
            with (result_dir / (name + '.log')).open('w') as log:
                result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            summary['checks'].append({'name': name, 'passed': result.returncode == 0})
            print(('PASS ' if result.returncode == 0 else 'FAIL ') + name, flush=True)
            if result.returncode: raise RuntimeError(name + ' failed')
        print('Testing synthetic camera transport and recovery…', flush=True)
        subprocess.run(['open', '-n', '-g', '--stdout', str(result_dir / 'app.log'), '--stderr', str(result_dir / 'app-error.log'),
                        str(app), '--args', '--check-camera-transport', str(args.seconds), str(report_path), run_id], check=True)
        deadline = time.monotonic() + args.seconds + 60
        while not report_path.exists():
            if time.monotonic() >= deadline:
                raise RuntimeError('Camera test timed out. Quit the test app before retrying; no pass was recorded.')
            time.sleep(0.5)
        report = json.loads(report_path.read_text())
        if report.get('runID') != run_id: raise RuntimeError('Camera report belongs to another run')
        summary['camera'] = report
        for check in report.get('checks', []):
            print(('PASS ' if check['passed'] else 'FAIL ') + check['name'], flush=True)
        if not report.get('passed'): raise RuntimeError(report.get('error', 'Camera transport checks failed'))
        summary['passed'] = True
        print('PASS unattended suite', flush=True)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        summary['error'] = str(error)
        print('FAIL: ' + str(error), file=sys.stderr)
    finally:
        awake.terminate()
        awake.wait()
        summary['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        (result_dir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print('Results: ' + str(result_dir / 'summary.json'), flush=True)
    return 0 if summary['passed'] else 1

if __name__ == '__main__':
    sys.exit(main())
