"""Exercise device PSRAM allocation paths, failures and ownership with a heap double."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MusicPathsTests(unittest.TestCase):
    def test_psram_growth_failure_move_cache_failure_and_scan_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / 'esp_heap_caps.h').write_text(r'''
#pragma once
#include <cstdlib>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <unordered_map>
constexpr int MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
inline bool fail_alloc=false;
inline int live_allocations=0;
inline size_t live_bytes=0, peak_bytes=0;
inline std::unordered_map<void*,size_t> sizes;
inline void* heap_caps_realloc(void* ptr, size_t size, int caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(fail_alloc) return nullptr;
    void* result=std::malloc(size);
    if(!result) return nullptr;
    live_bytes+=size;
    peak_bytes=std::max(peak_bytes,live_bytes);
    if(ptr) {
        const size_t old=sizes.at(ptr);
        std::memcpy(result,ptr,std::min(old,size));
        live_bytes-=old;
        sizes.erase(ptr);
        std::free(ptr);
    } else ++live_allocations;
    sizes[result]=size;
    return result;
}
inline void heap_caps_free(void* ptr) {
    if(ptr) {
        --live_allocations;
        live_bytes-=sizes.at(ptr);
        sizes.erase(ptr);
    }
    std::free(ptr);
}
''')
            source = folder/'test.cc'
            source.write_text(r'''
#include "music_index_cache.h"
#include <cassert>
#include <fstream>
#include <string>
int main(int argc,char** argv) {
    std::string root=argv[1], cache=root+"/index.bin";
    {
        TfMusic::MusicPaths paths;
        for(unsigned i=0;i<16;++i) assert(paths.push_back("/sdcard/song.mp3"));
        fail_alloc=true;
        assert(!paths.push_back("/sdcard/other.mp3"));
        assert(paths.size()==16 && paths[0].view()=="/sdcard/song.mp3");
        fail_alloc=false;
        // Realloc may move the underlying buffer: aliasing input must survive.
        assert(paths.push_back(paths[0].view()));
        assert(paths[16].view()=="/sdcard/song.mp3");
        TfMusic::MusicPaths moved=std::move(paths);
        assert(paths.empty() && moved.size()==17);
        TfMusic::MusicPaths target;
        assert(target.push_back("/sdcard/replaced.mp3"));
        target=std::move(moved);
        assert(moved.empty() && target.size()==17);
        while(target.size()<TfMusic::kMaxTracks) assert(target.push_back("/sdcard/a.mp3"));
        assert(!target.push_back("/sdcard/overflow.mp3"));
        assert(target.storage_bytes()==244*1024);
    }
    assert(live_allocations==0);
    {
        TfMusic::Index source;
        assert(source.paths.push_back(root+"/song.mp3"));
        source.path_bytes=source.paths[0].size()+1;
        assert(TfMusic::SaveCache(cache,root,source,44));
        TfMusic::Index loaded;
        uint32_t generation=0;
        assert(TfMusic::LoadCache(cache,root,loaded,generation));
        fail_alloc=true;
        assert(!TfMusic::LoadCache(cache,root,loaded,generation));
        assert(generation==44 && loaded.paths==source.paths);
        std::ofstream(root+"/song.mp3").put('x');
        TfMusic::Index failed;
        TfMusic::Scan(root,root,0,failed);
        assert(failed.memory_error && failed.paths.empty());
        fail_alloc=false;
        TfMusic::Index recovered;
        TfMusic::Scan(root,root,0,recovered);
        assert(!recovered.memory_error && recovered.paths.size()==1);
    }
    assert(live_allocations==0 && live_bytes==0);
    {
        peak_bytes=0;
        TfMusic::Index old_index, new_index;
        for(unsigned i=0;i<1024;++i) {
            const std::string path=root+"/song-"+std::to_string(10000+i)+".mp3";
            assert(old_index.paths.push_back(path));
            assert(new_index.paths.push_back(path));
            old_index.path_bytes+=path.size()+1;
            new_index.path_bytes+=path.size()+1;
        }
        assert(TfMusic::SaveCache(cache,root,new_index,45));
        // The third table reserves exactly the validated count. Even a realloc
        // implementation that always moves stays inside the three-table bound.
        assert(peak_bytes==3*244*1024);
    }
    assert(live_allocations==0 && live_bytes==0);
}
''')
            exe=folder/'test'
            subprocess.run(shlex.split(os.environ.get('CXX','c++'))+[
                '-std=c++17','-DESP_PLATFORM','-I'+str(folder),
                '-I'+str(ROOT/'main/boards/lckfb/szpi-esp32s3'),
                '-I'+str(ROOT/'main/audio'),str(source),'-o',str(exe)],
                check=True,capture_output=True,text=True)
            subprocess.run([str(exe),str(folder)],check=True,capture_output=True,text=True)
