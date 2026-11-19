"""Exercise the production tap policy without accessing the device."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AssistantTapTests(unittest.TestCase):
    def test_gesture_rejection_and_recovery(self):
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / 'test.cc'
            src.write_text(r'''
#include "assistant_tap.h"
#include <cassert>
#include <limits>
int main() {
    AssistantTap tap;
    assert(!tap.End(100, 100, 100, true)); // no press
    tap.Begin(100, 100, 100, true);
    assert(tap.End(104, 102, 180, true));
    assert(!tap.End(104, 102, 181, true)); // no duplicate release
    tap.Begin(100, 100, 200, true);
    assert(!tap.End(100, 100, 280, true)); // debounce
    tap.Begin(100, 100, 600, true);
    assert(tap.End(100, 100, 680, true)); // recovery after debounce

    AssistantTap drag;
    drag.Begin(100, 100, 0, true);
    drag.Move(113, 100);
    drag.Move(100, 100);
    assert(!drag.End(100, 100, 100, true)); // moved out then back
    drag.Begin(100, 100, 200, true);
    assert(!drag.End(100, 113, 300, true)); // move only at release
    drag.Begin(100, 100, 400, true);
    assert(drag.End(112, 100, 450, true)); // small jitter tolerated

    AssistantTap duration;
    duration.Begin(0, 0, 0, true);
    assert(!duration.End(0, 0, 19, true));
    duration.Begin(0, 0, 100, true);
    assert(!duration.End(0, 0, 600, true)); // long hold
    duration.Begin(0, 0, 700, true);
    assert(duration.End(0, 0, 720, true));

    AssistantTap blocked;
    blocked.Begin(0, 0, 0, false);
    assert(!blocked.End(0, 0, 100, true)); // press in a blocked page
    blocked.Begin(0, 0, 200, true);
    assert(!blocked.End(0, 0, 300, false)); // page opens before release
    blocked.Begin(0, 0, 400, true);
    blocked.Cancel();
    assert(!blocked.End(0, 0, 500, true)); // lost press/menu transition
    blocked.Begin(0, 0, 600, true);
    assert(blocked.End(0, 0, 700, true));

    AssistantTap wrap;
    const auto max = std::numeric_limits<uint32_t>::max();
    wrap.Begin(0, 0, max - 50, true);
    assert(wrap.End(0, 0, 30, true)); // LVGL tick wrap
    wrap.Begin(0, 0, 300, true);
    assert(wrap.End(0, 0, 400, true));
}
''')
            exe = Path(temp) / 'test'
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'main/boards/lckfb/szpi-esp32s3'),
                str(src), '-o', str(exe)], check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True)
