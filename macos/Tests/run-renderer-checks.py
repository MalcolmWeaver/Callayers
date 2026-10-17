#!/usr/bin/env python3
"""Run real SDK checks while observing attempted requests to a local HTTP server."""
import http.server
import os
import subprocess
import sys
import threading


def main():
    requests = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            self.send_response(200)
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(b'network should not reach this server')

        def log_message(self, *_args):
            pass

    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            env = dict(os.environ, CALLAYERS_TEST_NETWORK_URL=f'http://127.0.0.1:{server.server_port}/blocked')
            result = subprocess.run(sys.argv[1:], env=env, timeout=90)
        finally:
            server.shutdown()
            thread.join()
    if requests:
        print('FAIL: isolated card reached the network', file=sys.stderr)
        return 1
    if result.returncode == 0:
        print('PASS no requests reached the local HTTP observer')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
