"""Exercise the exact HTTP worker embedded in the payload, on loopback."""
import concurrent.futures
import json
import os
import pathlib
import socket
import subprocess
import time
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

    def post_config(self, revision, text, save=0, title='', editor_header=True):
        body = f'revision={revision}\nsave={save}\ntitle={title}\n{text}'.encode()
        headers = {'Content-Type': 'application/x-keyboardsense-config'}
        if editor_header:
            headers['X-KeyboardSense'] = 'editor-v1'
        request = urllib.request.Request(self.url + '/api/config', data=body, method='POST', headers=headers)
        with urllib.request.urlopen(request, timeout=4) as response:
            self.assertEqual(response.status, 202)
            return json.loads(response.read())['id']

    def await_command(self, request_id):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            _, body = self.get('/api/state')
            state = json.loads(body)
            if state['command']['id'] == request_id:
                return state
            time.sleep(.01)
        self.fail('input thread did not acknowledge settings')

    def test_configuration_apply_save_and_rejection(self):
        _, body = self.get('/api/state')
        initial = json.loads(body)
        old_revision = initial['profile']['revision']
        self.assertGreater(old_revision, 0)
        request_id = self.post_config(old_revision, 'sensitivity=2.4\nbind.usb_1d=cross\n')
        state = self.await_command(request_id)
        self.assertEqual(state['command']['status'], 0)
        self.assertEqual(state['config']['sensitivity'], 2.4)
        self.assertEqual(state['config']['keys'][29], 6)
        self.assertEqual(state['config']['keys'][44], 6)  # preserve default alias
        _, body = self.get('/api/global')
        self.assertEqual(json.loads(body)['config']['sensitivity'], 1)
        request_id = self.post_config(state['profile']['revision'], 'sensitivity=2.4\n', save=1)
        state = self.await_command(request_id)
        self.assertEqual(state['command']['status'], 0)
        _, body = self.get('/api/global')
        self.assertEqual(json.loads(body)['config']['sensitivity'], 2.4)
        for revision, text, title, header, status in [
            (old_revision, 'sensitivity=1\n', '', True, 409),
            (state['profile']['revision'], 'sensitivity=nan\n', '', True, 400),
            (state['profile']['revision'], 'sensitivity=1\n', '../bad', True, 400),
            (state['profile']['revision'], 'sensitivity=1\n', '', False, 403),
            (state['profile']['revision'], 'bind.usb_41=cross\n', '', True, 400),
        ]:
            with self.subTest(text=text, status=status), self.assertRaises(urllib.error.HTTPError) as error:
                self.post_config(revision, text, title=title, editor_header=header)
            self.assertEqual(error.exception.code, status)
            error.exception.close()
        _, body = self.get('/api/state')
        self.assertEqual(json.loads(body)['config']['sensitivity'], 2.4)

    def test_fragmented_settings_body_does_not_block_telemetry(self):
        _, body = self.get('/api/state')
        revision = json.loads(body)['profile']['revision']
        data = f'revision={revision}\nsave=0\ntitle=\nsensitivity=1.7\n'.encode()
        header = (f'POST /api/config HTTP/1.1\r\nHost: localhost\r\nX-KeyboardSense: editor-v1\r\n'
                  f'Content-Type: application/x-keyboardsense-config\r\nContent-Length: {len(data)}\r\n\r\n').encode()
        with socket.create_connection(('127.0.0.1', self.port), timeout=4) as connection:
            connection.sendall(header + data[:5])
            _, body = self.get('/api/state')
            self.assertEqual(json.loads(body)['source'], 'preview')
            connection.sendall(data[5:])
            response = b''
            while chunk := connection.recv(4096):
                response += chunk
            self.assertIn(b'202 Accepted', response)
            request_id = json.loads(response.split(b'\r\n\r\n', 1)[1])['id']
            self.assertEqual(self.await_command(request_id)['command']['status'], 0)
