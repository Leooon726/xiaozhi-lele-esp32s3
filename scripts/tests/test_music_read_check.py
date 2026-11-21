"""Production read-check bounds and async SD ownership, without hardware writes."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / 'main/boards/lckfb/szpi-esp32s3'


def compile_run(code):
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp)
        (path / 'test.cc').write_text(code)
        p = subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=c++23', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
            '-I' + str(BOARD), str(path / 'test.cc'), '-o', str(path / 'test')],
            capture_output=True, text=True)
        if p.returncode:
            raise AssertionError(p.stderr)
        subprocess.run([str(path / 'test')], check=True)


class MusicReadCheckTests(unittest.TestCase):
    def test_fragmented_reads_bounds_errors_and_cancellation(self):
        compile_run(r'''
#include "music_read_check.h"
#include <cassert>
#include <cstring>
#include <string>
int main() {
    using namespace TfMusic;
    uint8_t buffer[4];
    for (const auto mode : {"normal", "limit", "oversize", "read_error", "bad_count",
                            "hash_error", "cancel_before", "cancel_after"}) {
        std::string input=std::string(mode)=="oversize" ? "123456789" : "12345678";
        std::string collected;
        size_t pos=0;int calls=0;bool cancelled=std::string(mode)=="cancel_before";
        auto result=CheckReadStream([&](uint8_t* out,size_t size){
            ++calls;
            if (std::string(mode)=="read_error" && pos>=3) return -1;
            if (std::string(mode)=="bad_count") return int(size+1);
            size_t n=std::min({size,size_t(3),input.size()-pos});
            memcpy(out,input.data()+pos,n);pos+=n;
            if (std::string(mode)=="cancel_after") cancelled=true;
            return int(n);
        }, [&](const uint8_t* data,size_t n){
            if (std::string(mode)=="hash_error") return false;
            collected.append(reinterpret_cast<const char*>(data),n);return true;
        }, [&](){return cancelled;},buffer,sizeof(buffer),8);
        if (std::string(mode)=="normal" || std::string(mode)=="limit") {
            assert(result.status==ReadCheckStatus::Complete && result.bytes==8 && collected==input);
        } else if (std::string(mode)=="oversize") {
            assert(result.status==ReadCheckStatus::TooLarge && result.bytes<=8 && pos==9);
        } else if (std::string(mode)=="read_error" || std::string(mode)=="bad_count") {
            assert(result.status==ReadCheckStatus::ReadError);
        } else if (std::string(mode)=="hash_error") {
            assert(result.status==ReadCheckStatus::HashError && collected.empty());
        } else {
            assert(result.status==ReadCheckStatus::Cancelled && collected.empty());
            assert(calls==(std::string(mode)=="cancel_before" ? 0 : 1));
        }
    }
    auto never=[](uint8_t*,size_t)->int{assert(false);return 0;};
    auto update=[](const uint8_t*,size_t){return true;};
    auto cancel=[](){return false;};
    assert(CheckReadStream(never,update,cancel,buffer,0,8).status==ReadCheckStatus::ReadError);
    assert(CheckReadStream(never,update,cancel,nullptr,4,8).status==ReadCheckStatus::ReadError);
    assert(CheckReadStream(never,update,cancel,buffer,4,SIZE_MAX).status==ReadCheckStatus::ReadError);
}
''')

    def test_known_pattern_checks_crypto_status_length_and_digest(self):
        import hashlib
        source = (BOARD / 'music_tools.cc').read_text()
        start = source.index('bool CheckReadBufferMemory(uint8_t* buffer) {')
        end = source.index('\nvoid RunReadCheck(', start)
        expected = ','.join(str(x) for x in hashlib.sha256(bytes(range(256)) * 16).digest())
        code = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
constexpr int PSA_ALG_SHA_256=1, PSA_SUCCESS=0;
int mode=0;
int psa_hash_compute(int algorithm, const uint8_t* data,size_t size,uint8_t* out,size_t cap,size_t* length){
 assert(algorithm==1 && size==4096 && cap==32);
 for(size_t i=0;i<size;++i)assert(data[i]==uint8_t(i));
 const uint8_t reference[]={REFERENCE};
 memcpy(out,reference,32);*length=mode==2 ? 31 : 32;
 if(mode==3)out[0]^=1;
 return mode==1 ? -1 : 0;
}
''' .replace('REFERENCE', expected) + source[start:end] + r'''
int main(){uint8_t buffer[4096];
 for(mode=0;mode<4;++mode)assert(CheckReadBufferMemory(buffer)==(mode==0));
}
'''
        compile_run(code)

    def test_worker_reserves_storage_and_releases_after_close(self):
        source = (BOARD / 'music_tools.cc').read_text()
        start = source.index('void StartReadCheck(unsigned index, bool internal = false) {')
        end = source.index('\nvoid RunDebugCommand(', start)
        compile_run(r'''
#include <cassert>
#include <functional>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
constexpr int kDeviceStateIdle=3,pdPASS=1,MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
std::mutex card_mutex;
bool storage_pending=false,debug_verify_active=false,scanning=false,ready=true;
std::atomic<bool> debug_verify_cancel=false;
struct Path { std::string value="/sdcard/song.mp3";const std::string& view(){return value;} };
std::vector<Path> library(1);
struct ReadCheckJob {std::string path;unsigned index;bool internal=false;};
struct Audio {bool idle=true;bool IsPlaybackIdle(){return idle;}};
struct Application {
    std::deque<std::function<void()>> pending;Audio audio;int state=3;bool blocked=false;
    static Application& GetInstance(){static Application a;return a;}
    void Schedule(std::function<void()> f){pending.push_back(std::move(f));}
    void Drain(){while(!pending.empty()){auto f=std::move(pending.front());pending.pop_front();f();}}
    int GetDeviceState(){return state;}
    Audio& GetAudioService(){return audio;}
    void SetMusicStorageBlocked(bool b){blocked=b;}
};
void (*worker)(void*)=nullptr;void* argument=nullptr;bool fail_task=false;
int created=0,checked=0,deleted=0;
int xTaskCreateWithCaps(void(*f)(void*),const char*,int stack,void* arg,int priority,void*,int caps){
    assert(debug_verify_active && Application::GetInstance().blocked);
    assert(stack==6144 && priority==1 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    ++created;if(fail_task)return 0;worker=f;argument=arg;return pdPASS;
}
void vTaskDeleteWithCaps(void*){++deleted;}
void RunReadCheck(const ReadCheckJob& job){
    assert(debug_verify_active && Application::GetInstance().blocked && job.index==0);
    assert(job.path=="/sdcard/song.mp3");++checked;
}
''' + source[start:end] + r'''
int main(){
 auto& app=Application::GetInstance();
 auto reject=[&](){int before=created;StartReadCheck(0);app.Drain();assert(created==before && !debug_verify_active && !app.blocked);};
 storage_pending=true;reject();storage_pending=false;
 scanning=true;reject();scanning=false;
 ready=false;reject();ready=true;
 app.state=5;reject();app.state=3;
 app.audio.idle=false;reject();app.audio.idle=true;
 StartReadCheck(1);app.Drain();assert(created==0);
 fail_task=true;StartReadCheck(0);app.Drain();assert(created==1 && !app.blocked && !debug_verify_active);
 fail_task=false;StartReadCheck(0);assert(created==1);app.Drain();assert(created==2 && debug_verify_active && app.blocked);
 StartReadCheck(0);app.Drain();assert(created==2); // Only one active worker.
 worker(argument);assert(checked==1 && deleted==1 && debug_verify_active && app.blocked);
 app.Drain();assert(!debug_verify_active && !app.blocked); // No ownership release before read scope exits.
 StartReadCheck(0,true);app.Drain();assert(static_cast<ReadCheckJob*>(argument)->internal);storage_pending=true;
 worker(argument);app.Drain();assert(!debug_verify_active && app.blocked); // Transfer keeps its gate.
}
''')


if __name__ == '__main__':
    unittest.main()
