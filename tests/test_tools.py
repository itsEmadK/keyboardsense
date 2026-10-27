import contextlib
import io
import pathlib
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from check_elf import check
import send_payload


def payload():
    data = bytearray(0x101)
    data[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<HHIQQ', data, 16, 3, 62, 1, 0x100, 64)
    struct.pack_into('<HH', data, 54, 56, 1)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0, 0, 0, len(data), len(data), 0x4000)
    return data


class PayloadTests(unittest.TestCase):
    def test_accepts_x86_64_payload(self):
        check(payload())
        data = payload()
        struct.pack_into('<Q', data, 24, 0)  # SDK _start at virtual address zero
        check(data)

    def test_rejects_wrong_arch_truncation_and_bad_segments(self):
        for length in (0, 4, 63, 100):
            with self.subTest(length=length), self.assertRaises(ValueError):
                check(payload()[:length])
        for offset, fmt, value in (
            (18, '<H', 183),  # ARM binary
            (24, '<Q', 0x10000),  # entry outside a segment
            (64 + 4, '<I', 4),  # no executable segment
            (64 + 32, '<Q', 0x1000),  # load extends past file
            (64 + 48, '<Q', 0x1000),  # wrong PS5 alignment
            (64 + 16, '<Q', 1),  # incongruent load address
        ):
            data = payload()
            struct.pack_into(fmt, data, offset, value)
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                check(data)

    def test_sender_checks_payload_before_connecting(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'bad.elf'
            path.write_bytes(b'not a payload')
            with mock.patch.object(sys, 'argv', ['send_payload.py', '192.0.2.1', str(path)]), \
                 mock.patch.object(send_payload.socket, 'create_connection') as connect:
                with self.assertRaises(ValueError):
                    send_payload.main()
                connect.assert_not_called()

    def test_sender_transmits_entire_elf_and_half_closes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'pad.elf'
            data = payload()
            path.write_bytes(data)
            with mock.patch.object(sys, 'argv', ['send_payload.py', '192.0.2.1', str(path)]), \
                 mock.patch.object(send_payload.socket, 'create_connection') as connect, \
                 contextlib.redirect_stdout(io.StringIO()):
                send_payload.main()
                connect.assert_called_once_with(('192.0.2.1', 9021), timeout=10)
                connection = connect.return_value.__enter__.return_value
                connection.sendall.assert_called_once_with(bytes(data))
                connection.shutdown.assert_called_once_with(send_payload.socket.SHUT_WR)
