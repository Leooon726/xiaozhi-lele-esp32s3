"""Offline checks for dangerous flash-plan inputs; no serial device is opened."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
try:
    import serial
except ImportError:
    serial = None


@unittest.skipIf(serial is None, 'pyserial needed for device helper import')
class LeleFlashPlanTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('lele_device', ROOT/'scripts/lele/device.py')
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name)
        self.files = {'0x0':'boot.bin', '0x8000':'part.bin', '0xd000':'ota.bin',
                      '0x20000':'app.bin', '0x800000':'assets.bin'}
        for name in self.files.values():
            (self.build/name).write_bytes(b'x'*64)
        app = bytearray(64)
        app[0] = 0xE9
        app[12] = 9
        app[32:50] = b'lichuang-dev-lele'
        (self.build/'app.bin').write_bytes(app)
        self.write_args()

    def write_args(self):
        (self.build/'flasher_args.json').write_text(json.dumps({'flash_files': self.files}))

    def test_valid_five_partition_plan(self):
        self.assertEqual(len(self.module.load_flash_files(self.build)), 5)

    def test_nvs_sector_overlap_rejected(self):
        (self.build/'part.bin').write_bytes(b'x'*4097)
        with self.assertRaisesRegex(ValueError, 'NVS'):
            self.module.load_flash_files(self.build)

    def test_board_chip_mismatch_rejected(self):
        app = bytearray((self.build/'app.bin').read_bytes())
        app[12] = 0
        (self.build/'app.bin').write_bytes(app)
        with self.assertRaisesRegex(ValueError, 'ESP32-S3'):
            self.module.load_flash_files(self.build)

    def test_outside_build_path_rejected(self):
        self.files['0x0'] = '../outside.bin'
        self.write_args()
        with self.assertRaisesRegex(ValueError, 'Invalid build file'):
            self.module.load_flash_files(self.build)

    def test_outside_flash_rejected(self):
        self.files['0x1000000'] = self.files.pop('0x800000')
        self.write_args()
        with self.assertRaisesRegex(ValueError, 'outside 16 MiB'):
            self.module.load_flash_files(self.build)
