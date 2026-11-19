"""Run the production TF index on a Mac/Linux filesystem; no device is accessed."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "main/boards/lckfb/szpi-esp32s3"


class TfMusicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        src = folder / "index.cc"
        cls.exe = folder / "index"
        src.write_text(r'''
#include "music_library.h"
#include <iostream>
int main(int argc, char** argv) {
    std::string mode = argv[1];
    if (mode == "valid") {
        std::cout << TfMusic::ValidPath(argv[2]);
    } else if (mode == "match") {
        std::cout << TfMusic::Matches(argv[2], argv[3]);
    } else if (mode == "format") {
        std::cout << SdAudio::Extension(argv[2]) << "\n"
                  << SdAudio::Candidate(argv[2]) << "\n" << SdAudio::Playable(argv[2]);
    } else {
        TfMusic::Index index;
        TfMusic::Scan(argv[2], argv[2], 0, index);
        std::sort(index.paths.begin(), index.paths.end());
        std::cout << index.truncated << " " << index.path_bytes << "\n";
        for (const auto& path : index.paths) std::cout << path << "\n";
    }
}
''')
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
            "-I" + str(HEADER), "-I" + str(ROOT / "main/audio"), str(src), "-o", str(cls.exe),
        ], check=True, capture_output=True, text=True)

    def run_index(self, *args):
        return subprocess.check_output([str(self.exe), *map(str, args)], text=True)

    def test_rejects_escape_wrong_root_and_non_audio(self):
        for path in ["/sdcard/music/../secret.mp3", "/sdcard2/music/a.mp3",
                     "/sdcard/music/cover.jpg", "/sdcard/music/./a.mp3",
                     "/sdcard/music//a.mp3", "/sdcard/music/a\\b.mp3",
                     "/sdcard/music/" + "a" * 240 + ".mp3"]:
            with self.subTest(path=path):
                self.assertEqual(self.run_index("valid", path), "0")

    def test_chinese_paths_and_uppercase_extensions(self):
        self.assertEqual(self.run_index("valid", "/sdcard/music/歌手/晴天.MP3"), "1")
        self.assertEqual(self.run_index("match", "歌手 - 晴天.MP3", "晴天"), "1")
        self.assertEqual(self.run_index("match", "Singer - Song.MP3", "song"), "1")
        self.assertEqual(self.run_index("match", "晴天.mp3", "雨天"), "0")

    def test_scan_real_directories_filters_and_preserves_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "歌手").mkdir()
            for name in ["歌手/晴天.MP3", "test.mp3", "cover.jpg", ".hidden.mp3", "._test.mp3"]:
                (root / name).write_bytes(b"unchanged")
            before = {str(p): p.read_bytes() for p in root.rglob("*") if p.is_file()}
            lines = self.run_index("scan", root).splitlines()
            self.assertEqual(lines[0].split()[0], "0")
            self.assertEqual(set(lines[1:]), {str(root / "test.mp3"), str(root / "歌手/晴天.MP3"), str(root / ".hidden.mp3")})
            self.assertEqual(before, {str(p): p.read_bytes() for p in root.rglob("*") if p.is_file()})

    def test_card_root_mixed_formats_and_system_metadata(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            other_formats = ["录音.WAV", "album/song.FLAC", "album/a.AAC", "a.m4a",
                     "b.ogg", "c.opus", "d.wma", "e.aif", "f.aiff", "g.ape", "h.m4b", "i.alac",
                     "extra.wav"]
            names = ["root.MP3", "album/中文歌名.mp3", ".hidden/曲目.mp3"]
            for name in names + other_formats + ["cover.jpg", "._root.mp3", ".Spotlight-V100/not-music.mp3",
                                 "System Volume Information/ignore.mp3"]:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            lines = self.run_index("scan", root).splitlines()
            self.assertEqual(lines[0].split()[0], "0")
            self.assertEqual(set(lines[1:]), {str(root / name) for name in names})
            for ext in ["wav", "flac", "aac", "m4a", "ogg", "opus", "wma", "aif", "aiff", "ape", "m4b", "alac"]:
                self.assertEqual(self.run_index("format", "file." + ext).splitlines(), ["." + ext, "0", "0"])
            self.assertEqual(self.run_index("format", "file.MP3").splitlines(), [".mp3", "1", "1"])
            self.assertEqual(self.run_index("format", "file.jpg").splitlines(), [".jpg", "0", "0"])

    def test_track_and_byte_caps(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for i in range(150):
                (root / (str(i) + "a" * 100 + ".mp3")).touch()
            lines = self.run_index("scan", root).splitlines()
            flag, count_bytes = map(int, lines[0].split())
            self.assertEqual(flag, 1)
            self.assertLessEqual(len(lines) - 1, 128)
            self.assertLessEqual(count_bytes, 16 * 1024)

    def test_missing_directory_and_deep_nesting(self):
        self.assertEqual(self.run_index("scan", "/no-such-music-folder").splitlines(), ["0 0"])
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            deep = root / "a/b/c/d/e/f/g/h/i/j"
            deep.mkdir(parents=True)
            (deep / "hidden.mp3").touch()
            lines = self.run_index("scan", root).splitlines()
            self.assertEqual(lines[0].split()[0], "0")
            self.assertEqual(lines[1:], [str(deep / "hidden.mp3")])


if __name__ == "__main__":
    unittest.main()
