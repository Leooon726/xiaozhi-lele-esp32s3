"""Run the production cache format and idle policy on a host filesystem."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "main/boards/lckfb/szpi-esp32s3"


class MusicIndexCacheTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        driver = folder / "cache.cc"
        cls.exe = folder / "cache"
        driver.write_text(r'''
#include "music_index_cache.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
static TfMusic::Index MakeIndex(const std::string& root, const std::string& name) {
    TfMusic::Index index;
    index.paths.push_back(root + "/" + name);
    index.path_bytes = index.paths[0].size() + 1;
    return index;
}
int main(int argc, char** argv) {
    std::string mode = argv[1], base = argv[2], root = base + "/music";
    std::string a = base + "/a.bin", b = base + "/b.bin";
    auto old = MakeIndex(root, "歌手 - 晴天.MP3");
    auto fresh = MakeIndex(root, "new.mp3");
    if (mode == "policy") {
        TfMusic::IdleScanPolicy policy;
        assert(!policy.CanRun(false, false, 0));
        assert(!policy.CanRun(false, true, 100));
        assert(!policy.CanRun(false, true, 10099));
        assert(policy.CanRun(false, true, 10100));
        assert(!policy.CanRun(false, false, 10101));
        assert(!policy.CanRun(false, true, 11000));
        assert(!policy.CanRun(false, true, 20999));
        assert(policy.CanRun(false, true, 21000));
        assert(policy.CanRun(true, false, 21001));
        assert(!policy.CanRun(false, true, 21002));
        TfMusic::IdleScanPolicy wrap;
        assert(!wrap.CanRun(false, true, 0xfffffff0u));
        assert(wrap.CanRun(false, true, uint32_t(0xfffffff0u + 10000u)));
        assert(TfMusic::NewerGeneration(0, 0xffffffffu));
        assert(!TfMusic::NewerGeneration(0xffffffffu, 0));
        return 0;
    }
    assert(TfMusic::SaveCache(a, root, old, 10));
    TfMusic::Index loaded;
    uint32_t generation = 0;
    assert(TfMusic::LoadCache(a, root, loaded, generation));
    assert(loaded.paths == old.paths && generation == 10);
    if (mode == "roundtrip") {
        // Song files and even the music directory do not need to be touched at cache load.
        assert(TfMusic::ValidIndex(loaded, root));
    } else if (mode == "newest") {
        fresh.truncated = true;
        assert(TfMusic::SaveCache(b, root, fresh, 11));
        assert(TfMusic::LoadLatestCache(a, b, root, loaded, generation) == 1);
        assert(loaded.paths == fresh.paths && loaded.truncated && generation == 11);
    } else if (mode == "interrupted") {
        assert(TfMusic::SaveCache(b, root, fresh, 11));
        // Emulate power loss midway through writing the inactive slot.
        assert(truncate(b.c_str(), 31) == 0);
        assert(TfMusic::LoadLatestCache(a, b, root, loaded, generation) == 0);
        assert(loaded.paths == old.paths && generation == 10);
    } else if (mode == "corrupt") {
        std::fstream f(a, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(30); f.put('x'); f.close();
        auto unchanged = loaded;
        assert(!TfMusic::LoadCache(a, root, loaded, generation));
        assert(loaded.paths == unchanged.paths);
        assert(TfMusic::LoadLatestCache(a, b, root, loaded, generation) == -1);
    } else if (mode == "legacy") {
        std::fstream f(a, std::ios::in | std::ios::out | std::ios::binary);
        f.write("LLMIDX01", 8); f.close();
        assert(!TfMusic::LoadCache(a, root, loaded, generation));
    } else if (mode == "wrong_root") {
        assert(!TfMusic::LoadCache(a, base + "/other", loaded, generation));
    } else if (mode == "bad_header") {
        std::fstream f(a, std::ios::in | std::ios::out | std::ios::binary);
        uint8_t count[4]; TfMusic::Put32(count, 1000000);
        f.seekp(12); f.write(reinterpret_cast<char*>(count), 4); f.close();
        assert(!TfMusic::LoadCache(a, root, loaded, generation));
    } else if (mode == "trailing_bytes") {
        std::ofstream f(a, std::ios::app | std::ios::binary); f << 'x'; f.close();
        assert(!TfMusic::LoadCache(a, root, loaded, generation));
    } else if (mode == "failed_save") {
        assert(!TfMusic::SaveCache(base + "/absent/b.bin", root, fresh, 11));
        assert(TfMusic::LoadCache(a, root, loaded, generation));
        assert(loaded.paths == old.paths);
        fresh.io_error = true;
        assert(!TfMusic::SaveCache(a, root, fresh, 11));
        assert(TfMusic::LoadCache(a, root, loaded, generation));
        assert(loaded.paths == old.paths);
    } else if (mode == "invalid_paths") {
        auto invalid = MakeIndex(root, "../escape.mp3");
        assert(!TfMusic::SaveCache(b, root, invalid, 11));
        fresh.paths.push_back(fresh.paths[0]); fresh.path_bytes *= 2;
        assert(!TfMusic::SaveCache(b, root, fresh, 11));
    } else if (mode == "checkpoint") {
        int calls = 0;
        assert(TfMusic::SaveCache(b, root, fresh, 11, [&]() { ++calls; }));
        assert(calls == 2);
        TfMusic::Index scan;
        TfMusic::Scan(base, base, 0, scan, [&]() { ++calls; });
        assert(calls > 2);
        TfMusic::Index failed;
        TfMusic::Scan(a, a, 0, failed);
        assert(failed.io_error);
    } else if (mode == "empty") {
        TfMusic::Index empty;
        assert(TfMusic::SaveCache(b, root, empty, 11));
        assert(TfMusic::LoadLatestCache(a, b, root, loaded, generation) == 1);
        assert(loaded.paths.empty());
    }
    std::cout << mode << ": passed\n";
}
''')
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
            "-I" + str(HEADER), "-I" + str(ROOT / "main/audio"), str(driver), "-o", str(cls.exe),
        ], check=True, capture_output=True, text=True)

    def run_case(self, mode):
        with tempfile.TemporaryDirectory() as folder:
            subprocess.run([str(self.exe), mode, folder], check=True, capture_output=True, text=True)

    def test_roundtrip_without_enumerating_songs(self): self.run_case("roundtrip")
    def test_latest_valid_generation(self): self.run_case("newest")
    def test_interrupted_inactive_slot_keeps_old_cache(self): self.run_case("interrupted")
    def test_checksum_corruption_does_not_replace_output(self): self.run_case("corrupt")
    def test_old_folder_only_cache_is_rejected(self): self.run_case("legacy")
    def test_wrong_root_is_rejected(self): self.run_case("wrong_root")
    def test_untrusted_header_is_bounded(self): self.run_case("bad_header")
    def test_trailing_bytes_are_rejected(self): self.run_case("trailing_bytes")
    def test_save_or_scan_failure_preserves_old_cache(self): self.run_case("failed_save")
    def test_paths_and_duplicates_are_validated(self): self.run_case("invalid_paths")
    def test_scan_and_save_use_cooperative_checkpoints(self): self.run_case("checkpoint")
    def test_empty_library_can_replace_old_index(self): self.run_case("empty")
    def test_idle_pause_manual_override_and_clock_wrap(self): self.run_case("policy")


if __name__ == "__main__":
    unittest.main()
