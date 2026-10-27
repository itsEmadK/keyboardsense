"""Exercise the exact HTTP worker embedded in the payload, on loopback."""
import concurrent.futures
import json
import os
import pathlib
import socket
import subprocess
import unittest
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]


class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = os.environ.get('KS_PREVIEW_BINARY', str(ROOT / 'build/preview'))
        cls.process = subprocess.Popen([binary, '0'], cwd=ROOT,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        line = cls.process.stdout.readline().strip()
        if not line.startswith('Local URL: '):
            cls.process.wait(timeout=5)
            raise RuntimeError(cls.process.stderr.read() or line)
        cls.url = line.removeprefix('Local URL: ').rstrip('/')
        cls.port = int(cls.url.rsplit(':', 1)[1])

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate()
        cls.process.wait(timeout=5)
        cls.process.stdout.close()
        cls.process.stderr.close()

    def get(self, path):
        with urllib.request.urlopen(self.url + path, timeout=4) as response:
            return response.headers, response.read()

    def test_embedded_page_and_assets(self):
        headers, body = self.get('/')
        self.assertIn('text/html', headers['Content-Type'])
        self.assertIn(b'id="keyboard"', body)
        self.assertIn(b'Virtual DualSense', body)
        self.assertEqual(int(headers['Content-Length']), len(body))
        for path, expected in [('/app.mjs', 'text/javascript'),
                               ('/assets/dualsense/base.png', 'image/png'),
                               ('/assets/keys/shift.svg', 'image/svg+xml'),
                               ('/CREDITS.txt', 'text/plain')]:
            with self.subTest(path=path):
                headers, body = self.get(path)
                self.assertIn(expected, headers['Content-Type'])
                self.assertEqual(int(headers['Content-Length']), len(body))
                self.assertGreater(len(body), 0)

    def test_state_is_readonly_and_labeled(self):
        headers, body = self.get('/api/state?cache=0')
        state = json.loads(body)
        self.assertEqual(state['source'], 'preview')
        self.assertEqual(len(state['config']['keys']), 256)
        self.assertEqual(state['keys'], [])
        self.assertEqual(state['pad']['lx'], 128)
        self.assertEqual(headers['Cache-Control'], 'no-store')
        request = urllib.request.Request(self.url + '/api/state', data=b'{"keys":[26]}', method='POST')
        with self.assertRaises(urllib.error.HTTPError) as error:
            urllib.request.urlopen(request, timeout=4)
        self.assertEqual(error.exception.code, 405)
        error.exception.close()
        _, body = self.get('/api/state')
        self.assertEqual(json.loads(body)['keys'], [])

    def test_unmapped_paths_do_not_access_filesystem(self):
        for path in ('/does-not-exist', '/../LICENSE', '/%2e%2e/NOTICE', '/api/input'):
            with self.subTest(path=path), self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(self.url + path, timeout=4)
            self.assertEqual(error.exception.code, 404)
            error.exception.close()

    def test_partial_request_does_not_block_other_clients(self):
        with socket.create_connection(('127.0.0.1', self.port), timeout=4) as slow:
            slow.sendall(b'GET /api/state HTTP/1.1\r\nHost: local\r\n')
            _, body = self.get('/api/state')
            self.assertEqual(json.loads(body)['source'], 'preview')
            slow.sendall(b'\r\n')
            self.assertIn(b'HTTP/1.1 200 OK', slow.recv(1024))

    def test_oversized_header_is_bounded(self):
        with socket.create_connection(('127.0.0.1', self.port), timeout=4) as connection:
            connection.sendall(b'GET / HTTP/1.1\r\nX: ' + b'a' * 3000)
            self.assertIn(b'431 Request Header Too Large', connection.recv(1024))

    def test_parallel_asset_and_snapshot_requests(self):
        paths = ['/api/state', '/style.css', '/assets/dualsense/base.png'] * 2
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            results = list(pool.map(self.get, paths))
        self.assertEqual(len(results), 6)
        for headers, body in results:
            self.assertEqual(int(headers['Content-Length']), len(body))
