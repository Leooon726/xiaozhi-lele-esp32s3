"""Production page snapshots and UI playback validation under index changes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / 'main/boards/lckfb/szpi-esp32s3'


class MusicBrowserTests(unittest.TestCase):
    def test_bounded_pages_and_stale_selection(self):
        source = (BOARD / 'music_tools.cc').read_text()
        start = source.index('TfMusic::BrowserPage GetLichuangMusicPage(')
        end = source.index('\n#ifdef CONFIG_LICHUANG_TF_MUSIC_DEBUG', start)
        harness = r'''
#include "music_browser.h"
#include "music_library.h"
#include <cassert>
#include <mutex>
#include <limits>
std::mutex card_mutex;
TfMusic::MusicPaths library;
uint32_t library_revision=1;
bool ready=false,scanning=true,library_truncated=false,storage_pending=false,debug_verify_active=false;
std::string index_error;
class Application {
public:
    std::string requested;
    unsigned requests=0;
    static Application& GetInstance(){static Application a;return a;}
    void RequestMusic(std::string path){requested=std::move(path);++requests;}
};
''' + source[start:end] + r'''
int main() {
    auto empty=GetLichuangMusicPage(999);
    assert(empty.count==0 && empty.total==0 && empty.offset==0 && !empty.ready && empty.scanning);
    for(unsigned i=0;i<1024;++i)
        assert(library.push_back("/sdcard/folder/song"+std::to_string(i)+".mp3"));
    ready=true;
    auto first=GetLichuangMusicPage(0);
    assert(first.count==5 && first.total==1024 && first.offset==0 && first.revision==1);
    auto aligned=GetLichuangMusicPage(7);
    assert(aligned.offset==5 && aligned.count==5 && aligned.paths[0]=="/sdcard/folder/song5.mp3");
    auto last=GetLichuangMusicPage(std::numeric_limits<size_t>::max());
    assert(last.offset==1020 && last.count==4 && last.paths[3]=="/sdcard/folder/song1023.mp3");
    const auto selected=first.paths[0];
    assert(RequestLichuangMusicPath(selected));
    assert(Application::GetInstance().requested==selected);
    assert(!RequestLichuangMusicPath("/sdcard/../secret.mp3"));
    assert(!RequestLichuangMusicPath("/sdcard/not-indexed.mp3"));
    ready=false;
    assert(!RequestLichuangMusicPath(selected));
    ready=true;storage_pending=true;
    assert(GetLichuangMusicPage(0).blocked);
    assert(!RequestLichuangMusicPath(selected));
    storage_pending=false;debug_verify_active=true;
    assert(GetLichuangMusicPage(0).blocked && !RequestLichuangMusicPath(selected));
    debug_verify_active=false;

    // Scanner publishes a different index: old row zero must never play the new row zero.
    TfMusic::MusicPaths replacement;
    assert(replacement.push_back("/sdcard/new.mp3"));
    library=std::move(replacement);++library_revision;
    assert(first.paths[0]==selected); // snapshot owns its five paths
    assert(!RequestLichuangMusicPath(selected));
    auto shrunk=GetLichuangMusicPage(last.offset);
    assert(shrunk.total==1 && shrunk.offset==0 && shrunk.count==1 && shrunk.revision==2);
    assert(RequestLichuangMusicPath(shrunk.paths[0]));
    assert(Application::GetInstance().requested=="/sdcard/new.mp3");
    index_error="mount failed";library_truncated=true;scanning=false;
    auto status=GetLichuangMusicPage(0);
    assert(status.error==index_error && status.truncated && !status.scanning);
    assert(TfMusic::BrowserName("/sdcard/中文歌.MP3")=="中文歌");
    assert(TfMusic::BrowserName("/sdcard/A/同名.mp3")=="同名 [A]");
    assert(TfMusic::BrowserName("/sdcard/B/同名.mp3")=="同名 [B]");
}
'''
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / 'test.cc'
            src.write_text(harness)
            exe = Path(temp) / 'test'
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(BOARD),
                '-I' + str(ROOT / 'main/audio'), str(src), '-o', str(exe)],
                check=True, capture_output=True, text=True)
            subprocess.run([str(exe)], check=True)
