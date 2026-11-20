"""Production upload filename, transactional file and HTTP lifecycle regressions."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main/boards/lckfb/szpi-esp32s3"


def run_cpp(folder, source, extra=()):
    path = folder / "test.cc"
    path.write_text(source, encoding="utf-8")
    binary = folder / "test"
    result = subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + str(folder),
        "-I" + str(BOARD), str(path), *map(str, extra), "-o", str(binary)
    ], capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stderr)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stderr)


class WifiUploadTests(unittest.TestCase):
    def test_names_transaction_abort_conflicts_and_full_disk(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            code = r'''
#include <cassert>
#include <fstream>
#include <filesystem>
#include <csignal>
#include <sys/resource.h>
#include "sd_file_manager.h"
using namespace LeleUpload;
namespace fs=std::filesystem;
std::string read(const std::string& p) {
    std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};
}
int main() {
    const std::string dir=ROOT_DIR;
    auto DecodeName=[](std::string_view text,std::string& name){
        Query q;if(!q.Parse(text)||q.has_path||!q.to.empty()||!q.op.empty()||q.offset||q.replace) return false;
        name=q.name;return ValidName(name);
    };
    std::string name;
    assert(DecodeName("name=%E4%B8%AD%E6%96%87%20song%2B1.mp3",name));
    assert(name=="中文 song+1.mp3");
    assert(DecodeName("name=a+b.txt",name) && name=="a b.txt");
    assert(ValidName(std::string(180,'a')) && !ValidName(std::string(181,'a')));
    for(const char* bad:{"","..",".hidden","../x","a/b","a\\b","x."," x","x ",
                         "a:b","a*b","a?b","NUL.mp3","con","LPT1.txt","COM9.a",
                         "a\nb","a\177b","\300\257","\355\240\200","\364\220\200\200"})
        assert(!ValidName(bad));
    for(const char* bad:{"name=..%2Fx","name=%00a","name=a%5Cb","name=%GG",
                         "name=%","name=%E4","name=ok&other=x","other=x"})
        assert(!DecodeName(bad,name));
    {
        File f; assert(f.Open(dir,"中文.mp3",4,"0001")==Result::Ok);
        assert(!f.Write("12345",5));
        assert(f.Write("1234",4) && f.Finish()==Result::Ok);
        assert(f.Finish()==Result::Invalid);
    }
    assert(read(dir+"/中文.mp3")=="1234");
    {
        File f;assert(f.Open(dir,"中文.mp3",4,"0002")==Result::Exists);
    }
    assert(read(dir+"/中文.mp3")=="1234");
    {
        File f;assert(f.Open(dir,"abort.mp3",10,"0003")==Result::Ok);
        assert(f.Write("1234",4) && f.Finish()==Result::Invalid);
    }
    assert(!fs::exists(dir+"/abort.mp3") && !fs::exists(dir+"/.lele-upload-0003.part"));
    {
        File f;assert(f.Open(dir,"cancel.mp3",4,"0004")==Result::Ok);
        assert(f.Write("1234",4)); // Cancel even after receiving all bytes, before commit.
    }
    assert(!fs::exists(dir+"/cancel.mp3"));
    {
        File f;assert(f.Open(dir,"race.mp3",4,"0005")==Result::Ok);
        std::ofstream(dir+"/race.mp3")<<"original";
        assert(f.Write("1234",4) && f.Finish()==Result::Exists);
    }
    assert(read(dir+"/race.mp3")=="original");
    std::ofstream(dir+"/.lele-upload-0006.part")<<"retain";
    {
        File f;assert(f.Open(dir,"collision.mp3",4,"0006")==Result::Io);
    }
    assert(read(dir+"/.lele-upload-0006.part")=="retain");
    {
        File f;assert(f.Open(dir,"zero.txt",0,"0007")==Result::Invalid);
        assert(f.Open(dir,"large.mp3",kMaxFileBytes+1,"0007")==Result::Invalid);
    }
    struct rlimit previous, limited;
    assert(getrlimit(RLIMIT_FSIZE,&previous)==0);
    limited=previous;limited.rlim_cur=64;
    std::signal(SIGXFSZ,SIG_IGN);
    assert(setrlimit(RLIMIT_FSIZE,&limited)==0);
    {
        File f;assert(f.Open(dir,"full.mp3",256,"0008")==Result::Ok);
        const std::string bytes(256,'x');
        const bool wrote=f.Write(bytes.data(),bytes.size());
        assert(!wrote || f.Finish()==Result::Io);
    }
    assert(setrlimit(RLIMIT_FSIZE,&previous)==0);
    assert(!fs::exists(dir+"/full.mp3") && !fs::exists(dir+"/.lele-upload-0008.part"));
}
'''.replace("ROOT_DIR", json.dumps(str(folder / "uploads")))
            run_cpp(folder, code)

    def test_fatfs_mount_root_does_not_require_mkdir(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            code = r'''
#include <cassert>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <fstream>
static unsigned invalid_root_calls=0;
static int fat_stat(const char* path, struct stat* info) {
    if (std::strcmp(path,ROOT_DIR)==0) {
        ++invalid_root_calls;errno=EINVAL;return -1;
    }
    return ::stat(path,info);
}
static int fat_mkdir(const char* path, mode_t mode) {
    if (std::strcmp(path,ROOT_DIR)==0) {
        ++invalid_root_calls;errno=EINVAL;return -1;
    }
    return ::mkdir(path,mode);
}
#define stat(...) fat_stat(__VA_ARGS__)
#define mkdir(...) fat_mkdir(__VA_ARGS__)
#include "wifi_upload_file.h"
#undef stat
#undef mkdir
int main() {
    LeleUpload::File upload;
    assert(upload.Open(ROOT_DIR,"root.bin",4,"0001")==LeleUpload::Result::Ok);
    assert(upload.Write("root",4));
    assert(upload.Finish()==LeleUpload::Result::Ok);
    assert(invalid_root_calls==0);
    std::ifstream input(std::string(ROOT_DIR)+"/root.bin");
    std::string data;input>>data;assert(data=="root");
}
'''.replace("ROOT_DIR", json.dumps(str(folder)))
            run_cpp(folder, code)

    def test_storage_gate_is_not_released_until_scan_is_quiet(self):
        source = (BOARD / "music_tools.cc").read_text()
        functions = ""
        for name in ("IsLichuangStorageReadyForUpload", "FinishLichuangStorageUpload"):
            start = source.index("bool " + name + "() {")
            functions += source[start:source.index("\n}\n", start) + 2] + "\n"
        code = r'''
#include <cassert>
#include <mutex>
std::mutex card_mutex;
bool storage_pending=false,scanning=false;
bool ready=true;
unsigned library_revision=1;
void* card=nullptr;
unsigned scans=0;
bool StartScanLocked(bool manual) {
    assert(manual && !storage_pending);++scans;return true;
}
''' + functions + r'''
int main() {
    assert(!IsLichuangStorageReadyForUpload() && !FinishLichuangStorageUpload());
    storage_pending=true;scanning=true;
    assert(!IsLichuangStorageReadyForUpload() && !FinishLichuangStorageUpload());
    assert(storage_pending && scans==0);
    scanning=false;
    assert(!IsLichuangStorageReadyForUpload());
    card=&scans;assert(IsLichuangStorageReadyForUpload());
    assert(FinishLichuangStorageUpload() && !storage_pending && scans==1);
    assert(!ready && library_revision==2);
    assert(!IsLichuangStorageReadyForUpload() && !FinishLichuangStorageUpload());
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            run_cpp(Path(temporary), code)

    def test_http_authorization_cancellation_timeout_and_stop_worker(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            headers = {
                "sdkconfig.h": "#define CONFIG_LICHUANG_USB_STORAGE 1\n",
                "esp_http_server.h": r'''
#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <net/if.h>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,HTTP_GET=0,HTTP_POST=1;
using httpd_handle_t=void*;
struct httpd_req_t {
    void* user_ctx=nullptr;size_t content_len=0;std::string key,query,body,response,status;
    std::string confirm,revision;
    size_t read=0;int chunk=4;bool broken=false,cancel=false,slow=false;
};
struct httpd_config_t {
    int server_port=0,ctrl_port=0,stack_size=0,task_priority=0,max_open_sockets=0;
    int max_uri_handlers=0,max_uri_len=0,recv_wait_timeout=0,send_wait_timeout=0;
    bool lru_purge_enable=false;struct ifreq* if_name=nullptr;
};
#define HTTPD_DEFAULT_CONFIG() {}
struct httpd_uri_t {
    const char* uri;int method;esp_err_t(*handler)(httpd_req_t*);void* user_ctx;
};
inline std::vector<httpd_uri_t> routes;
inline int stops=0;inline bool stop_error=false;
inline esp_err_t httpd_start(httpd_handle_t* handle,const httpd_config_t* config) {
    assert(config->server_port==8080 && config->if_name && config->max_uri_len>=545);
    *handle=reinterpret_cast<void*>(1);routes.clear();return ESP_OK;
}
inline esp_err_t httpd_stop(httpd_handle_t) { ++stops;return stop_error?ESP_FAIL:ESP_OK; }
inline esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t* r) {routes.push_back(*r);return 0;}
inline int httpd_resp_set_status(httpd_req_t* r,const char* s) {r->status=s;return 0;}
inline int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*) {return 0;}
inline int httpd_resp_set_type(httpd_req_t*,const char*) {return 0;}
inline int httpd_resp_send(httpd_req_t* r,const char* s,size_t n) {r->response.assign(s,n);return 0;}
inline int httpd_resp_sendstr(httpd_req_t* r,const char* s) {r->response=s;return 0;}
inline const std::string& header(httpd_req_t* r,const char* name) {
    return std::strcmp(name,"X-Lele-Confirm")==0?r->confirm:
           std::strcmp(name,"X-Lele-Revision")==0?r->revision:r->key;
}
inline size_t httpd_req_get_hdr_value_len(httpd_req_t* r,const char* name) {return header(r,name).size();}
inline int httpd_req_get_hdr_value_str(httpd_req_t* r,const char* name,char* p,size_t n) {
    const auto& value=header(r,name);
    if(value.size()>=n)return -1;
    std::memcpy(p,value.c_str(),value.size()+1);return 0;
}
inline size_t httpd_req_get_url_query_len(httpd_req_t* r) {return r->query.size();}
inline int httpd_req_get_url_query_str(httpd_req_t* r,char* p,size_t n) {
    if(r->query.size()>=n)return -1;
    std::memcpy(p,r->query.c_str(),r->query.size()+1);return 0;
}
void cancel_upload();
void advance_time();
inline int httpd_req_recv(httpd_req_t* r,char* p,size_t n) {
    if(r->broken)return -1;
    n=std::min({n,size_t(r->chunk),r->body.size()-r->read});
    std::memcpy(p,r->body.data()+r->read,n);r->read+=n;
    if(r->cancel){r->cancel=false;cancel_upload();}
    if(r->slow)advance_time();
    return int(n);
}
''',
                "esp_netif.h": r'''
#pragma once
inline void* esp_netif_get_handle_from_ifkey(const char*) {return reinterpret_cast<void*>(1);}
inline int esp_netif_get_netif_impl_name(void*,char* name) {std::strcpy(name,"lo");return 0;}
''',
                "esp_random.h": r'''
#pragma once
#include <cstdint>
inline uint32_t esp_random() {static uint32_t value=0x12345678;return ++value;}
''',
                "esp_timer.h": "#pragma once\n#include <cstdint>\ninline int64_t now=0;\ninline int64_t esp_timer_get_time(){return now;}\n",
                "freertos/FreeRTOS.h": "#pragma once\nconstexpr int pdPASS=1;\n",
                "freertos/task.h": r'''
#pragma once
inline void(*worker)(void*)=nullptr;inline void* worker_arg=nullptr;inline bool task_error=false;
inline int xTaskCreate(void(*fn)(void*),const char*,int,void* arg,int,void*) {
    if(task_error)return 0;
    worker=fn;worker_arg=arg;return 1;
}
inline void vTaskDelete(void*) {}
inline void vTaskDelay(int) {}
''',
                "wifi_manager.h": r'''
#pragma once
struct WifiManager {
    bool connected=true;static WifiManager& GetInstance(){static WifiManager w;return w;}
    bool IsConnected(){return connected;}std::string GetIpAddress(){return "192.168.1.2";}
};
''',
                "psa/crypto.h": r'''
#pragma once
#include <cstdint>
using psa_hash_operation_t=int;
#define PSA_HASH_OPERATION_INIT 0
constexpr int PSA_SUCCESS=0,PSA_ALG_SHA_256=1;
inline int psa_crypto_init(){return 0;}
inline int psa_hash_setup(int* op,int){*op=1;return 0;}
inline int psa_hash_update(int*,const uint8_t*,size_t){return 0;}
inline int psa_hash_abort(int* op){*op=0;return 0;}
inline int psa_hash_finish(int* op,uint8_t* out,size_t n,size_t* len){
    std::memset(out,0,n);*len=n;*op=0;return 0;
}
''',
            }
            for name, text in headers.items():
                path = folder / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            # Rebind only the device mount root to a host temp directory.
            production = (BOARD / "wifi_upload.cc").read_text().replace(
                '"/sdcard"', json.dumps(str(folder)))
            (folder / "wifi_upload.cc").write_text(production)
            code = r'''
#include <cassert>
#include <fstream>
#include <filesystem>
#include "wifi_upload.cc"
LeleWifiUpload* active=nullptr;
void cancel_upload(){active->RequestStop();}
void advance_time(){now+=61LL*1000000;}
void stop(LeleWifiUpload& upload){
    upload.RequestStop();assert(!upload.Stopped() && worker);
    auto fn=worker;worker=nullptr;fn(worker_arg);assert(upload.Stopped());
}
int call(LeleWifiUpload& upload,httpd_req_t& request) {
    request.user_ctx=&upload;return routes[1].handler(&request);
}
httpd_req_t request(LeleWifiUpload& upload,const std::string& name){
    httpd_req_t r;r.key=upload.Code();r.query="name="+name;
    r.body="testdata";r.content_len=r.body.size();return r;
}
int main(){
    namespace fs=std::filesystem;
    LeleWifiUpload upload;active=&upload;
    WifiManager::GetInstance().connected=false;assert(!upload.Start() && upload.Stopped());
    WifiManager::GetInstance().connected=true;assert(upload.Start() && !upload.Stopped());
    assert(routes.size()==5 && upload.Address()=="http://192.168.1.2:8080");
    httpd_req_t page;page.user_ctx=&upload;
    assert(routes[0].handler(&page)==ESP_OK);
    assert(page.response.find("X-Lele-Key")!=std::string::npos);
    assert(page.response.find(upload.Code())==std::string::npos);
    auto wrong=request(upload,"wrong.mp3");wrong.key="bad";
    assert(call(upload,wrong)==ESP_FAIL && wrong.status=="403 Forbidden");
    assert(wrong.read==0 && !fs::exists(ROOT_DIR "/wrong.mp3"));
    auto traversal=request(upload,"..%2Fsecret.mp3");
    assert(call(upload,traversal)==ESP_FAIL && traversal.status=="400 Bad Request");
    auto large=request(upload,"large.mp3");large.content_len=LeleUpload::kMaxFileBytes+1;
    assert(call(upload,large)==ESP_FAIL && large.status=="413 Content Too Large");
    auto valid=request(upload,"song.mp3");
    assert(call(upload,valid)==ESP_OK && upload.Saved()==1);
    assert(valid.response.find("\"bytes\":8")!=std::string::npos);
    auto collision=request(upload,"song.mp3");
    assert(call(upload,collision)==ESP_FAIL && collision.status=="409 Conflict");
    auto listing=request(upload,"");listing.content_len=0;listing.query="path=";listing.user_ctx=&upload;
    assert(routes[2].handler(&listing)==ESP_OK);
    assert(listing.response.find("\"name\":\"uploads\"")!=std::string::npos);
    auto info=listing;info.query="path=uploads/song.mp3";
    assert(routes[3].handler(&info)==ESP_OK && info.response.find("\"revision\":1")!=std::string::npos);
    auto replace=request(upload,"");replace.query="path=uploads/song.mp3&replace=1";
    assert(call(upload,replace)==ESP_FAIL && replace.status=="409 Conflict");
    replace.confirm="yes";replace.revision="0";
    assert(call(upload,replace)==ESP_FAIL && replace.status=="409 Conflict");
    replace.revision="1";replace.body="new data";
    assert(call(upload,replace)==ESP_OK && upload.Saved()==2);
    auto mkdir=listing;mkdir.query="op=mkdir&path=folder";
    assert(routes[4].handler(&mkdir)==ESP_OK && upload.Saved()==3);
    auto move=listing;move.query="op=move&path=uploads/song.mp3&to=folder/new.mp3";
    assert(routes[4].handler(&move)==ESP_FAIL && move.status=="409 Conflict");
    move.confirm="yes";move.revision="3";
    assert(routes[4].handler(&move)==ESP_OK && upload.Saved()==4);
    auto stale=listing;stale.query="path=&offset=1";stale.revision="3";
    assert(routes[2].handler(&stale)==ESP_FAIL && stale.status=="409 Conflict");
    auto hidden=listing;hidden.query="op=delete&path=.lele-music-index-a.bin";
    hidden.confirm="yes";hidden.revision="4";
    assert(routes[4].handler(&hidden)==ESP_FAIL && hidden.status=="400 Bad Request");
    auto nonempty=hidden;nonempty.query="op=delete&path=folder";
    assert(routes[4].handler(&nonempty)==ESP_FAIL && nonempty.status=="409 Conflict");
    auto removed=hidden;removed.query="op=delete&path=folder/new.mp3";
    assert(routes[4].handler(&removed)==ESP_OK && upload.Saved()==5);
    nonempty.revision="5";
    assert(routes[4].handler(&nonempty)==ESP_OK && upload.Saved()==6);
    auto broken=request(upload,"broken.mp3");broken.broken=true;
    assert(call(upload,broken)==ESP_FAIL && !fs::exists(ROOT_DIR "/broken.mp3"));
    auto slow=request(upload,"slow.mp3");slow.slow=true;
    assert(call(upload,slow)==ESP_FAIL && slow.status=="408 Request Timeout");
    assert(!fs::exists(ROOT_DIR "/slow.mp3"));
    auto cancelled=request(upload,"cancel.mp3");cancelled.cancel=true;
    assert(call(upload,cancelled)==ESP_FAIL && !fs::exists(ROOT_DIR "/cancel.mp3"));
    assert(upload.NeedsStop() && !upload.Stopped() && worker);
    worker(worker_arg);worker=nullptr;assert(upload.Stopped());
    assert(upload.Start());
    for(int i=0;i<8;++i){auto bad=request(upload,"x");bad.key="bad";assert(call(upload,bad)==ESP_FAIL);}
    assert(upload.NeedsStop());stop(upload);
    assert(upload.Start());now+=10LL*60*1000000+1;
    assert(upload.NeedsStop());stop(upload);
    assert(upload.Start());task_error=true;upload.RequestStop();
    assert(!upload.Stopped() && !worker && upload.NeedsStop());
    task_error=false;stop(upload);
    assert(upload.Start());stop_error=true;upload.RequestStop();worker(worker_arg);worker=nullptr;
    assert(!upload.Stopped() && upload.NeedsStop());
    stop_error=false;stop(upload);
    for(const auto& file:fs::directory_iterator(ROOT_DIR))
        assert(file.path().extension()!=".part");
}
'''.replace('ROOT_DIR', json.dumps(str(folder / "uploads")))
            run_cpp(folder, code)

    def test_menu_waits_for_files_to_close_before_resuming_storage(self):
        source = (BOARD / "lele_menu.cc").read_text()
        start = source.index("void LeleMenu::PollWifiUpload() {")
        function = source[start:source.index("\n}\n", start) + 2]
        harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
int64_t now=0;
int64_t esp_timer_get_time(){return now;}
bool quiet=false,mounted=false,pending=true;
unsigned cancelled=0,refreshed=0;
bool IsLichuangStorageQuiet(){return quiet;}
bool IsLichuangStorageReadyForUpload(){return mounted;}
bool mark_ok=true;
bool MarkLichuangStorageDirty(){return mark_ok;}
void CancelLichuangStorageTransfer(){pending=false;++cancelled;}
bool FinishLichuangStorageUpload(){pending=false;++refreshed;return true;}
struct Application {
    bool busy=false,blocked=true;
    static Application& GetInstance(){static Application a;return a;}
    bool IsMusicWorkerBusy(){return busy;}
    void SetMusicStorageBlocked(bool b){blocked=b;}
};
struct WifiManager {
    bool connected=true;
    static WifiManager& GetInstance(){static WifiManager w;return w;}
    bool IsConnected(){return connected;}
};
struct Upload {
    bool stopped=true,needs_stop=false,start_ok=true,requested=false;
    unsigned starts=0;
    void RequestStop(){requested=true;}
    bool Stopped(){return stopped;}
    bool NeedsStop(){return needs_stop;}
    bool Start(){++starts;if(start_ok)stopped=false;return start_ok;}
    unsigned Saved(){return 0;}
    std::string Address(){return "http://192.168.1.2:8080";}
    const char* Code(){return "12345678";}
};
struct LeleMenu {
    Upload wifi_upload_;
    bool wifi_closing_=false,wifi_started_=false,preparing_=true,wifi_transfer_=true;
    unsigned wifi_saved_=0;
    int64_t prepare_started_=0;
    const char* wifi_exit_message_="Transfer ended.";
    std::string message;
    void Message(const char* text){message=text;}
    void Show(const char* text){message=text;}
    void PollWifiUpload();
};
''' + function + r'''
int main(){
    auto& app=Application::GetInstance();
    LeleMenu menu;
    menu.PollWifiUpload();assert(menu.wifi_upload_.starts==0 && pending && app.blocked);
    quiet=true;mounted=true;app.busy=true;
    menu.PollWifiUpload();assert(menu.wifi_upload_.starts==0);
    app.busy=false;menu.PollWifiUpload();
    assert(menu.wifi_started_ && !menu.preparing_ && pending && app.blocked);
    assert(menu.message.find("12345678")!=std::string::npos);
    menu.wifi_closing_=true;menu.PollWifiUpload();
    assert(menu.wifi_upload_.requested && pending && app.blocked && refreshed==0);
    menu.wifi_upload_.stopped=true;menu.PollWifiUpload();
    assert(!pending && !app.blocked && refreshed==1 && !menu.wifi_transfer_);
    LeleMenu missing;mounted=false;pending=true;app.blocked=true;
    missing.PollWifiUpload();assert(missing.wifi_closing_ && pending && app.blocked);
    missing.PollWifiUpload();assert(!pending && !app.blocked && cancelled==1);
    LeleMenu marker;mounted=true;pending=true;app.blocked=true;mark_ok=false;
    marker.PollWifiUpload();assert(marker.wifi_closing_ && marker.wifi_upload_.starts==0);
    marker.PollWifiUpload();assert(!pending && !app.blocked && cancelled==2);
    assert(marker.message=="Cannot save SD state.");mark_ok=true;
    LeleMenu timeout;quiet=false;pending=true;app.blocked=true;now=30000001;
    timeout.PollWifiUpload();assert(timeout.wifi_closing_ && pending);
    timeout.PollWifiUpload();assert(!pending && !app.blocked && cancelled==3);
    LeleMenu disconnected;disconnected.preparing_=false;disconnected.wifi_started_=true;
    disconnected.wifi_upload_.stopped=false;pending=true;app.blocked=true;
    WifiManager::GetInstance().connected=false;
    disconnected.PollWifiUpload();assert(disconnected.wifi_closing_ && pending);
    disconnected.PollWifiUpload();assert(pending && app.blocked);
    disconnected.wifi_upload_.stopped=true;disconnected.PollWifiUpload();
    assert(!pending && !app.blocked && refreshed==2);
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            run_cpp(Path(temporary), harness)
