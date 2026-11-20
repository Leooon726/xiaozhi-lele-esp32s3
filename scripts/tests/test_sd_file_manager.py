"""Real host filesystem operations and replacement failure recovery."""
from pathlib import Path
import json
import tempfile
import unittest

from test_wifi_upload import BOARD, run_cpp


class SdFileManagerTests(unittest.TestCase):
    def test_paths_pagination_move_delete_and_protected_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            code = r'''
#include <cassert>
#include <filesystem>
#include <fstream>
#include <set>
#include "sd_file_manager.h"
using namespace LeleUpload;
namespace fs=std::filesystem;
int main(){
    const std::string root=ROOT_DIR;
    fs::create_directory(root+"/sd");fs::create_directory(root+"/outside");
    std::ofstream(root+"/outside/keep")<<"keep";
    Manager m(root+"/sd");
    Query q;
    assert(q.Parse("path=%E4%B8%AD%E6%96%87%2Fa%2Bb.mp3&replace=1"));
    assert(q.path=="中文/a+b.mp3" && q.replace && ValidPath(q.path));
    assert(q.Parse("path=&offset=123") && q.path.empty() && q.offset==123 && !q.replace);
    for(const char* query:{"path=x&path=y","path=%GG","replace=yes","offset=-1",
                           "offset=16385","path=x&","unknown=x"})
        assert(!q.Parse(query));
    for(const char* path:{"/absolute","../x","a/../b","a//b","a/","a/.cache",
                          ".lele-music-index-a.bin","System Volume Information/a",
                          "$RECYCLE.BIN/a","SYSTEM~1/a","LELE-M~1.BIN"})
        assert(!ValidPath(path));
    assert(ValidPath("") && ValidPath("中文/歌曲.mp3"));
    assert(m.Mkdir("中文")==Result::Ok && m.Mkdir("dest")==Result::Ok);
    assert(m.Mkdir("中文")==Result::Exists);
    assert(m.Mkdir("missing/child")==Result::NotFound);
    std::ofstream(root+"/sd/中文/song.mp3")<<"old";
    fs::create_directory_symlink(root+"/outside",root+"/sd/link");
    fs::create_symlink(root+"/outside/keep",root+"/sd/alias");
    Entry info;
    assert(m.Info("link/keep",info)==Result::Invalid);
    assert(m.Remove("alias")==Result::Invalid);
    assert(m.Move("中文/song.mp3","link/new")==Result::Invalid);
    assert(fs::exists(root+"/outside/keep"));
    assert(m.Remove("")==Result::Invalid && m.Move("","x")==Result::Invalid);
    assert(m.Move("中文","中文/sub")==Result::Invalid);
    assert(m.Remove("中文")==Result::NotEmpty);
    assert(m.Move("中文/song.mp3","dest/新歌.mp3")==Result::Ok);
    assert(!fs::exists(root+"/sd/中文/song.mp3") && fs::file_size(root+"/sd/dest/新歌.mp3")==3);
    std::ofstream(root+"/sd/dest/existing.mp3")<<"preserve";
    assert(m.Move("dest/新歌.mp3","dest/existing.mp3")==Result::Exists);
    assert(fs::file_size(root+"/sd/dest/existing.mp3")==8);
    assert(m.Move("dest","renamed")==Result::Ok);
    assert(m.Remove("renamed/新歌.mp3")==Result::Ok);
    assert(m.Remove("renamed/existing.mp3")==Result::Ok && m.Remove("renamed")==Result::Ok);
    assert(m.Remove("中文")==Result::Ok);
    for(unsigned i=0;i<85;++i)std::ofstream(root+"/sd/f"+std::to_string(i))<<i;
    std::ofstream(root+"/sd/.private")<<"private";
    fs::create_directory(root+"/sd/System Volume Information");
    Page page;
    size_t offset=0;
    std::set<std::string> seen;
    unsigned pages=0;
    do{
        assert(m.List("",offset,page)==Result::Ok && page.entries.size()<=kPageEntries);
        for(const auto& entry:page.entries)assert(seen.insert(entry.name).second);
        assert(!page.limited);
        offset=page.next;++pages;assert(pages<10);
    }while(page.more);
    assert(seen.size()==85 && pages==3);
    assert(!seen.count(".private") && !seen.count("alias") && !seen.count("link"));
    assert(m.List("",kDirectoryBudget+1,page)==Result::Invalid);
}
'''.replace("ROOT_DIR", json.dumps(str(root)))
            run_cpp(root, code)

    def test_replacement_abort_rollback_recovery_and_retained_backup(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            code = r'''
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <filesystem>
int InjectRename(const char*,const char*);
int InjectUnlink(const char*);
int InjectSync(int);
#define rename InjectRename
#define unlink InjectUnlink
#define fsync InjectSync
#include "wifi_upload_file.h"
#undef rename
#undef unlink
#undef fsync
using namespace LeleUpload;
int calls=0,fail_first=0,fail_second=0;
bool fail_sync=false,keep_backup=false;
int InjectRename(const char* from,const char* to){
    ++calls;
    if(calls==fail_first||calls==fail_second){errno=EIO;return -1;}
    return ::rename(from,to);
}
int InjectUnlink(const char* path){
    if(keep_backup&&std::string(path).find(".lele-old-")!=std::string::npos){errno=EIO;return -1;}
    return ::unlink(path);
}
int InjectSync(int fd){if(fail_sync){errno=EIO;return -1;}return ::fsync(fd);}
std::string read(const std::string& path){
    std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};
}
int main(){
    namespace fs=std::filesystem;
    const std::string dir=ROOT_DIR;
    std::ofstream(dir+"/song.mp3")<<"original";
    {
        File f;assert(f.Open(dir,"song.mp3",8,"aa",true)==Result::Ok);
        assert(f.Write("new",3)); // Aborted upload must never touch the original.
    }
    assert(read(dir+"/song.mp3")=="original" && calls==0);
    fail_sync=true;
    {
        File f;assert(f.Open(dir,"song.mp3",3,"ab",true)==Result::Ok);
        assert(f.Write("new",3) && f.Finish()==Result::Io);
    }
    assert(read(dir+"/song.mp3")=="original" && calls==0);fail_sync=false;
    fail_first=2;calls=0;
    {
        File f;assert(f.Open(dir,"song.mp3",3,"ac",true)==Result::Ok);
        assert(f.Write("new",3) && f.Finish()==Result::Io);
    }
    assert(read(dir+"/song.mp3")=="original" && calls==3);
    fail_first=2;fail_second=3;calls=0;
    {
        File f;assert(f.Open(dir,"song.mp3",3,"ad",true)==Result::Ok);
        assert(f.Write("new",3) && f.Finish()==Result::Recovery);
    }
    assert(!fs::exists(dir+"/song.mp3"));
    assert(read(dir+"/.lele-old-ad-song.mp3")=="original");
    assert(!fs::exists(dir+"/.lele-upload-ad.part"));
    assert(::rename((dir+"/.lele-old-ad-song.mp3").c_str(),(dir+"/song.mp3").c_str())==0);
    fail_first=fail_second=0;calls=0;
    {
        File f;assert(f.Open(dir,"song.mp3",3,"ae",true)==Result::Ok);
        assert(f.Write("new",3) && f.Finish()==Result::Ok && !f.BackupRetained());
    }
    assert(read(dir+"/song.mp3")=="new" && !fs::exists(dir+"/.lele-old-ae-song.mp3"));
    keep_backup=true;calls=0;
    {
        File f;assert(f.Open(dir,"song.mp3",4,"af",true)==Result::Ok);
        assert(f.Write("last",4) && f.Finish()==Result::Ok && f.BackupRetained());
    }
    assert(read(dir+"/song.mp3")=="last" && read(dir+"/.lele-old-af-song.mp3")=="new");
    std::ofstream(dir+"/.lele-old-bb-song.mp3")<<"retain";
    {
        File f;assert(f.Open(dir,"song.mp3",3,"bb",true)==Result::Ok);
        assert(f.Write("bad",3) && f.Finish()==Result::Io);
    }
    assert(read(dir+"/song.mp3")=="last" && read(dir+"/.lele-old-bb-song.mp3")=="retain");
}
'''.replace("ROOT_DIR", json.dumps(str(folder)))
            run_cpp(folder, code)

    def test_persistent_rebuild_marker_is_conservative_on_errors(self):
        source = (BOARD / "music_tools.cc").read_text()
        start = source.index("bool IndexNeedsRefresh() {")
        functions = source[start:source.index("#endif", start)]
        code = r'''
#include <cassert>
#include <cstdint>
using nvs_handle_t=int;
constexpr int ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,NVS_READONLY=0,NVS_READWRITE=1;
int open_error=0,get_error=0,set_error=0,commit_error=0,closed=0;
uint8_t persisted=0,pending_value=0;
int nvs_open(const char*,int,nvs_handle_t* h){*h=1;return open_error;}
int nvs_get_u8(int,const char*,uint8_t* v){*v=persisted;return get_error;}
void nvs_close(int){++closed;}
int nvs_set_u8(int,const char*,uint8_t v){pending_value=v;return set_error;}
int nvs_commit(int){if(!commit_error)persisted=pending_value;return commit_error;}
''' + functions + r'''
int main(){
    open_error=ESP_ERR_NVS_NOT_FOUND;assert(!IndexNeedsRefresh());
    open_error=2;assert(IndexNeedsRefresh() && !WriteIndexDirty(true));
    open_error=0;get_error=ESP_ERR_NVS_NOT_FOUND;assert(!IndexNeedsRefresh());
    get_error=2;assert(IndexNeedsRefresh());get_error=0;
    assert(WriteIndexDirty(true) && IndexNeedsRefresh());
    commit_error=2;assert(!WriteIndexDirty(false) && IndexNeedsRefresh());
    commit_error=0;assert(WriteIndexDirty(false) && !IndexNeedsRefresh());
    set_error=2;assert(!WriteIndexDirty(true));assert(closed>0);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            run_cpp(Path(temporary), code)
