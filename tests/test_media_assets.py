# SPDX-License-Identifier: GPL-3.0-only
import pathlib
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import embed_media


class MediaAssetTests(unittest.TestCase):
    def test_media_title_opens_only_local_webui(self):
        param, _ = embed_media.metadata()
        self.assertEqual(param['titleId'], 'KBSN01405')
        self.assertEqual(param['applicationCategoryType'], 65536)
        self.assertEqual(param['deeplinkUri'], 'http://127.0.0.1:1405/')
        self.assertEqual(param['contentId'], 'IV9999-KBSN01405_00-KEYBOARDSENSE001')
        self.assertEqual(len(param['contentId']), 36)

    def test_embedded_icon_is_complete_512px_rgba_png(self):
        data = embed_media.icon_png()
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        compressed = bytearray()
        offset = 8
        while offset < len(data):
            length, = struct.unpack_from('>I', data, offset)
            kind = data[offset + 4:offset + 8]
            payload = data[offset + 8:offset + 8 + length]
            crc, = struct.unpack_from('>I', data, offset + 8 + length)
            self.assertEqual(crc, zlib.crc32(kind + payload))
            if kind == b'IHDR':
                self.assertEqual(struct.unpack('>IIBBBBB', payload), (512, 512, 8, 6, 0, 0, 0))
            if kind == b'IDAT':
                compressed.extend(payload)
            offset += length + 12
        self.assertEqual(offset, len(data))
        self.assertEqual(kind, b'IEND')
        self.assertEqual(len(zlib.decompress(compressed)), 512 * (1 + 512 * 4))
