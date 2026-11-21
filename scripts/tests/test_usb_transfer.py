"""Exercise the actual device handoff code with SDK/USB/NVS fault injection."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / 'main/boards/lckfb/szpi-esp32s3'

STUB = r'''
#pragma once
#include <cassert>
#include <cstdio>
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_STATE=2, ESP_ERR_NVS_NOT_FOUND=3;
#define CONFIG_LICHUANG_USB_STORAGE 1
#define ESP_LOGI(...) do {} while(0)
#define ESP_LOGE(...) do {} while(0)
using nvs_handle_t = unsigned;
constexpr int NVS_READONLY=0, NVS_READWRITE=1;
inline int open_error=ESP_ERR_NVS_NOT_FOUND, get_error=ESP_OK, commit_error=ESP_OK;
inline int persistent_mode=0, staged_mode=0, committed=0;
inline int nvs_open(const char*, int rw, nvs_handle_t* h) {
    *h=1; return rw==NVS_READWRITE ? ESP_OK : open_error;
}
inline int nvs_get_i32(unsigned, const char*, int32_t* m) {*m=persistent_mode;return get_error;}
inline int nvs_set_i32(unsigned,const char*,int32_t m) {staged_mode=m;return ESP_OK;}
inline int nvs_commit(unsigned) {
    if(commit_error==ESP_OK) {persistent_mode=staged_mode;open_error=ESP_OK;++committed;}
    return commit_error;
}
inline void nvs_close(unsigned) {}
struct sdmmc_card_t {};
struct sdmmc_host_t {int max_freq_khz=0,slot=1;};
struct sdmmc_slot_config_t {int width=0,clk=0,cmd=0,d0=0,flags=0;};
#define SDMMC_HOST_DEFAULT() sdmmc_host_t{}
#define SDMMC_SLOT_CONFIG_DEFAULT() sdmmc_slot_config_t{}
constexpr int SDMMC_FREQ_DEFAULT=20000, SDMMC_SLOT_FLAG_INTERNAL_PULLUP=1;
constexpr int GPIO_NUM_47=47, GPIO_NUM_48=48, GPIO_NUM_21=21;
inline int sd_error=ESP_OK, host_closed=0;
inline int sdmmc_host_init() {return ESP_OK;}
inline int sdmmc_host_init_slot(int,sdmmc_slot_config_t* c) {
    assert(c->width==1 && c->clk==47 && c->cmd==48 && c->d0==21);return ESP_OK;
}
inline int sdmmc_card_init(sdmmc_host_t*,sdmmc_card_t*) {return sd_error;}
inline int sdmmc_host_deinit() {++host_closed;return ESP_OK;}
enum tinyusb_msc_mount_point_t {TINYUSB_MSC_STORAGE_MOUNT_USB,TINYUSB_MSC_STORAGE_MOUNT_APP};
enum {TINYUSB_MSC_EVENT_MOUNT_START,TINYUSB_MSC_EVENT_MOUNT_COMPLETE,
      TINYUSB_MSC_EVENT_MOUNT_FAILED,TINYUSB_MSC_EVENT_FORMAT_REQUIRED,
      TINYUSB_MSC_EVENT_FORMAT_FAILED};
struct tinyusb_msc_event_t {int id; tinyusb_msc_mount_point_t mount_point;};
using tinyusb_msc_storage_handle_t = void*;
using callback_t=void(*)(void*,tinyusb_msc_event_t*,void*);
struct tinyusb_msc_driver_config_t {
    struct {bool auto_mount_off=false;} user_flags;
    callback_t callback=nullptr;
};
struct tinyusb_msc_storage_config_t {
    struct {sdmmc_card_t* card=nullptr;} medium;
    tinyusb_msc_mount_point_t mount_point;
    struct {
        char* base_path=nullptr;
        bool do_not_format=false;
        struct {bool format_if_mount_failed=false;int max_files=0;} config;
    } fat_fs;
};
inline callback_t callback=nullptr;
inline bool mounted=false, device_live=false;
inline int usb_stop_error=ESP_OK, delete_error=ESP_OK, delete_calls=0;
inline int tinyusb_msc_install_driver(tinyusb_msc_driver_config_t* c) {
    callback=c->callback;return ESP_OK;
}
inline int tinyusb_msc_new_storage_sdmmc(tinyusb_msc_storage_config_t* c,void** h) {
    assert(c->medium.card && c->mount_point==TINYUSB_MSC_STORAGE_MOUNT_USB);
    assert(c->fat_fs.do_not_format && !c->fat_fs.config.format_if_mount_failed);
    *h=(void*)1;return ESP_OK;
}
enum {TINYUSB_EVENT_ATTACHED,TINYUSB_EVENT_DETACHED};
struct tinyusb_event_t {int id;};
inline void (*device_callback)(tinyusb_event_t*,void*)=nullptr;
struct tinyusb_config_t {void (*event_cb)(tinyusb_event_t*,void*)=nullptr;};
#define TINYUSB_DEFAULT_CONFIG() tinyusb_config_t{}
inline int tinyusb_driver_install(tinyusb_config_t* c) {device_live=true;device_callback=c->event_cb;return ESP_OK;}
inline int tinyusb_driver_uninstall() {
    if(usb_stop_error==ESP_OK)device_live=false;return usb_stop_error;
}
inline int tinyusb_msc_delete_storage(void*) {
    assert(!device_live);++delete_calls;return delete_error;
}
inline int tinyusb_msc_uninstall_driver() {return ESP_OK;}
inline bool tud_mounted() {return mounted;}
inline const char* esp_err_to_name(int) {return "stub";}
inline FILE* FakeFreopen(const char*,const char*,FILE* stream) {return stream;}
inline void event(int id,tinyusb_msc_mount_point_t owner) {
    tinyusb_msc_event_t e{id,owner};callback((void*)1,&e,nullptr);
}
'''

HARNESS = r'''
#include "stub.h"
#define freopen FakeFreopen
#include "usb_transfer.cc"
#undef freopen
int main(int argc,char** argv) {
    using namespace LeleUsb;
    assert(ReadBootMode()==BootMode::Assistant);
    open_error=ESP_FAIL;assert(ReadBootMode()==BootMode::Refresh);
    open_error=ESP_OK;get_error=ESP_FAIL;assert(ReadBootMode()==BootMode::Refresh);
    get_error=ESP_OK;persistent_mode=99;assert(ReadBootMode()==BootMode::Refresh);
    persistent_mode=1;assert(ReadBootMode()==BootMode::Refresh); // Upgrade old stuck flag.
    assert(!TakeTransferBootRequest());
    commit_error=ESP_FAIL;
    assert(SaveBootMode(BootMode::Transfer)!=ESP_OK && persistent_mode==1);
    commit_error=ESP_OK;
    assert(SaveBootMode(BootMode::Transfer)==ESP_OK);
    assert(ReadBootMode()==BootMode::Transfer && persistent_mode==3);
    commit_error=ESP_FAIL;
    assert(!TakeTransferBootRequest() && persistent_mode==3); // Never export on consume failure.
    commit_error=ESP_OK;
    assert(TakeTransferBootRequest() && persistent_mode==2);
    assert(!TakeTransferBootRequest() && ReadBootMode()==BootMode::Refresh); // Next boot.
    if(argc>1 && argv[1][0]=='s') {
        sd_error=ESP_FAIL;assert(Start()!=ESP_OK);
        assert(Finish()==ESP_OK && host_closed==1);return 0;
    }
    assert(Start()==ESP_OK && !HostReleased());
    if(argc>1 && argv[1][0]=='c') {
        assert(Finish()==ESP_OK && host_closed==1);return 0;
    }
    mounted=true;
    tinyusb_event_t attached{TINYUSB_EVENT_ATTACHED};device_callback(&attached,nullptr);
    assert(Finish()!=ESP_OK && delete_calls==0); // Cannot leave while host owns disk.
    mounted=false;
    assert(!HostReleased() && Finish()!=ESP_OK); // Disconnect alone is not a completed handoff.
    mounted=true;
    event(TINYUSB_MSC_EVENT_MOUNT_START,TINYUSB_MSC_STORAGE_MOUNT_APP);
    assert(!HostReleased()); // A request is not completed ownership transfer.
    event(TINYUSB_MSC_EVENT_MOUNT_COMPLETE,TINYUSB_MSC_STORAGE_MOUNT_APP);
    mounted=false;assert(HostReleased()); // Eject can also deconfigure USB.
    if(argc>1 && argv[1][0]=='e') {
        assert(Finish()==ESP_OK && host_closed==1);return 0;
    }
    mounted=true;assert(HostReleased());
    if(argc>1 && argv[1][0]=='f') {
        event(TINYUSB_MSC_EVENT_FORMAT_REQUIRED,TINYUSB_MSC_STORAGE_MOUNT_APP);
        assert(Failed() && !HostReleased());
        assert(Finish()!=ESP_OK && delete_calls==0);return 0;
    }
    event(TINYUSB_MSC_EVENT_MOUNT_START,TINYUSB_MSC_STORAGE_MOUNT_USB);
    assert(!HostReleased()); // Host reconfiguration revokes prior eject.
    event(TINYUSB_MSC_EVENT_MOUNT_COMPLETE,TINYUSB_MSC_STORAGE_MOUNT_APP);
    assert(HostReleased());
    usb_stop_error=ESP_FAIL;
    assert(Finish()!=ESP_OK && delete_calls==0 && persistent_mode==2);
    usb_stop_error=ESP_OK;delete_error=ESP_ERR_INVALID_STATE;
    assert(Finish()!=ESP_OK && host_closed==0 && persistent_mode==2);
    delete_error=ESP_OK;
    assert(Finish()==ESP_OK && host_closed==1);
    assert(SaveBootMode(BootMode::Refresh)==ESP_OK && ReadBootMode()==BootMode::Refresh);
}
'''


class UsbTransferTests(unittest.TestCase):
    def test_partial_unmount_does_not_retain_freed_card(self):
        source = (BOARD / 'music_tools.cc').read_text()
        start = source.index('bool UnmountLichuangMusicCard() {')
        end = source.index('\n}\n', start) + 2
        function = source[start:end]
        harness = r'''
#include <cassert>
#include <mutex>
struct sdmmc_card_t {};
std::mutex card_mutex;
sdmmc_card_t* card = new sdmmc_card_t;
bool storage_pending=false, scanning=false, ready=true, debug_verify_active=false;
constexpr auto kMusicRoot="/sdcard";
constexpr int ESP_OK=0;
int result=-1,calls=0;
int esp_vfs_fat_sdcard_unmount(const char*, sdmmc_card_t* p) {
    assert(card==nullptr); // IDF can free this even when unregister fails.
    delete p;++calls;return result;
}
''' + function + r'''
int main() {
    assert(!UnmountLichuangMusicCard() && card && ready && calls==0);
    storage_pending=true;scanning=true;
    assert(!UnmountLichuangMusicCard() && card && calls==0);
    scanning=false;debug_verify_active=true;
    assert(!UnmountLichuangMusicCard() && card && calls==0);
    debug_verify_active=false;
    assert(!UnmountLichuangMusicCard() && !card && !ready && calls==1);
    assert(storage_pending); // Failure does not reopen the I/O gate.
    card=new sdmmc_card_t;ready=true;result=ESP_OK;
    assert(UnmountLichuangMusicCard() && !card && !ready && calls==2);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / 'test.cc').write_text(harness)
            exe = folder / 'test'
            result = subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++23', str(folder / 'test.cc'), '-o', str(exe)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_real_handoff_with_sdk_faults(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / 'stub.h').write_text(STUB)
            for header in ['sdkconfig.h', 'esp_err.h', 'esp_log.h', 'nvs.h',
                           'driver/sdmmc_host.h', 'sdmmc_cmd.h', 'tinyusb.h',
                           'tinyusb_default_config.h', 'tinyusb_msc.h', 'tusb.h']:
                path = folder / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "stub.h"\n')
            (folder / 'test.cc').write_text(HARNESS)
            exe = folder / 'test'
            result = subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++23', '-I' + str(folder), '-I' + str(BOARD),
                str(folder / 'test.cc'), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for scenario in ['normal', 'format', 'sd-failure', 'cancel-before-connect', 'eject-deconfigured']:
                result = subprocess.run([str(exe), scenario], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, scenario + result.stderr)
