"""Reproduce the startup ordering failure using the actual LVGL hit tester."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AssistantTouchLvglTests(unittest.TestCase):
    def test_startup_order_and_modal_hit_testing(self):
        if not (ROOT / 'managed_components/lvgl__lvgl/CMakeLists.txt').exists():
            self.skipTest('Fetch ESP-IDF component dependencies before running the LVGL test')
        if not shutil.which('cmake'):
            self.skipTest('CMake is required for the real LVGL regression')
        build = ROOT / '.local/lvgl-touch-host'
        subprocess.run(['cmake', '-S', str(ROOT / 'scripts/tests/lvgl_touch'),
                        '-B', str(build), '-DLELE_ROOT=' + str(ROOT),
                        '-DCMAKE_BUILD_TYPE=Release'], check=True, capture_output=True, text=True)
        subprocess.run(['cmake', '--build', str(build), '--parallel', '6'],
                       check=True, capture_output=True, text=True)
        subprocess.run([str(build / 'touch_hit_test')], check=True,
                       capture_output=True, text=True)
