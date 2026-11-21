"""Validate the bounded local music diagnostic command grammar."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MusicDebugCommandTests(unittest.TestCase):
    def test_commands_and_rejected_input(self):
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / 'test.cc'
            src.write_text(r'''
#include "music_debug_command.h"
#include <cassert>
#include <string>
int main() {
    using namespace TfMusic;
    assert(ParseDebugCommand("music-test stop").action == DebugAction::Stop);
    assert(ParseDebugCommand("music-test status").action == DebugAction::Status);
    for (unsigned i = 0; i < 1024; ++i) {
        auto c = ParseDebugCommand("music-test play " + std::to_string(i));
        assert(c.action == DebugAction::Play && c.index == i);
        c = ParseDebugCommand("music-test verify " + std::to_string(i));
        assert(c.action == DebugAction::Verify && c.index == i);
    }
    for (auto s : {"", "play 0", "music-test play", "music-test play ",
                   "music-test play -1", "music-test play +1", "music-test play 1024",
                   "music-test play 4294967296", "music-test play 1 trailing",
                   "music-test play /sdcard/a.mp3", "music-test stop now",
                   "music-test verify", "music-test verify -1", "music-test verify 1024",
                   "music-test verify /sdcard/a.mp3", "music-test verify 1 trailing",
                   "music-test refresh", "music-test status\n", "music-test play 1;reboot"})
        assert(ParseDebugCommand(s).action == DebugAction::Invalid);
}
''')
            exe = Path(temp) / 'test'
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++23', '-I' + str(ROOT / 'main/boards/lckfb/szpi-esp32s3'),
                str(src), '-o', str(exe)], check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True)
