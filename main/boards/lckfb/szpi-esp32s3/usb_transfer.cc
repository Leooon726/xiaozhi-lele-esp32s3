#include "usb_transfer.h"
#include "music_sd_clock.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <nvs.h>
#include <sdmmc_cmd.h>
#include <tinyusb.h>
#include <tinyusb_default_config.h>
#include <tinyusb_msc.h>
#include <tusb.h>
#include <atomic>
#include <cstdio>

namespace LeleUsb {
namespace {
constexpr const char* kTag = "LeleUsb";
tinyusb_msc_storage_handle_t storage = nullptr;
sdmmc_card_t card{};
bool host_initialized = false;
bool msc_initialized = false;
bool device_started = false;
std::atomic<bool> app_owned{false};
std::atomic<bool> failed{false};
std::atomic<bool> ever_attached{false};

void DeviceEvent(tinyusb_event_t* event, void*) {
    if (event->id == TINYUSB_EVENT_ATTACHED)
        ever_attached = true;
}

void StorageEvent(tinyusb_msc_storage_handle_t, tinyusb_msc_event_t* event, void*) {
    if (event->id == TINYUSB_MSC_EVENT_MOUNT_START) {
        app_owned = false;
    } else if (event->id == TINYUSB_MSC_EVENT_MOUNT_COMPLETE) {
        app_owned = event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP;
        ESP_LOGI(kTag, "Storage owner: %s", app_owned ? "app (host released)" : "USB");
    } else if (event->id == TINYUSB_MSC_EVENT_MOUNT_FAILED ||
               event->id == TINYUSB_MSC_EVENT_FORMAT_REQUIRED ||
               event->id == TINYUSB_MSC_EVENT_FORMAT_FAILED) {
        failed = true;
        app_owned = false;
        ESP_LOGE(kTag, "Storage event %d; never formatting card", event->id);
    }
}
}  // namespace

BootMode ReadBootMode() {
    nvs_handle_t handle;
    const auto opened = nvs_open("lele_usb", NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND)
        return BootMode::Assistant;
    // Only a valid, explicit one-shot request may enable MSC. Invalid or old
    // persistent modes recover into the assistant with a fresh directory scan.
    if (opened != ESP_OK)
        return BootMode::Refresh;
    int32_t mode = 0;
    const auto result = nvs_get_i32(handle, "mode", &mode);
    nvs_close(handle);
    if (result == ESP_ERR_NVS_NOT_FOUND)
        return BootMode::Assistant;
    if (result != ESP_OK || (mode != 0 && mode != 2 && mode != 3))
        return BootMode::Refresh;
    return static_cast<BootMode>(mode);
}

esp_err_t SaveBootMode(BootMode mode) {
    nvs_handle_t handle;
    auto result = nvs_open("lele_usb", NVS_READWRITE, &handle);
    if (result != ESP_OK)
        return result;
    result = nvs_set_i32(handle, "mode", static_cast<int32_t>(mode));
    if (result == ESP_OK)
        result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}

bool TakeTransferBootRequest() {
    if (ReadBootMode() != BootMode::Transfer)
        return false;
    // Persist the recovery target before USB is exposed. If consumption fails,
    // stay in assistant mode rather than creating another persistent trap.
    const auto result = SaveBootMode(BootMode::Refresh);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Cannot consume transfer request: %s", esp_err_to_name(result));
        return false;
    }
    return true;
}

esp_err_t Start() {
    // Avoid console fan-out into the Serial/JTAG peripheral after OTG takes
    // ownership of the shared PHY. Normal assistant boot keeps the USB console.
    if (!freopen("/dev/uart/0", "w", stdout) || !freopen("/dev/uart/0", "w", stderr))
        return ESP_FAIL;
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = TfMusic::kSdClockKhz;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = GPIO_NUM_47;
    slot.cmd = GPIO_NUM_48;
    slot.d0 = GPIO_NUM_21;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    auto result = sdmmc_host_init();
    if (result != ESP_OK)
        return result;
    host_initialized = true;
    result = sdmmc_host_init_slot(host.slot, &slot);
    if (result == ESP_OK)
        result = sdmmc_card_init(&host, &card);
    if (result != ESP_OK)
        return result;

    ESP_LOGI(kTag, "SD clock: requested=%d actual=%d kHz", TfMusic::kSdClockKhz,
             card.real_freq_khz);
    tinyusb_msc_driver_config_t driver = {};
    // 2.4.0 routes the SCSI eject through the same automatic APP remount path.
    // Setting auto_mount_off would disable eject notifications too. No assistant
    // tasks exist in this boot, so its local remount never races application I/O.
    driver.user_flags.auto_mount_off = false;
    driver.callback = StorageEvent;
    result = tinyusb_msc_install_driver(&driver);
    if (result != ESP_OK)
        return result;
    msc_initialized = true;
    tinyusb_msc_storage_config_t config = {};
    config.medium.card = &card;
    config.mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB;
    config.fat_fs.base_path = const_cast<char*>("/sdcard");
    config.fat_fs.do_not_format = true;
    config.fat_fs.config.format_if_mount_failed = false;
    config.fat_fs.config.max_files = 4;
    result = tinyusb_msc_new_storage_sdmmc(&config, &storage);
    if (result != ESP_OK)
        return result;
    auto usb = TINYUSB_DEFAULT_CONFIG();
    usb.event_cb = DeviceEvent;
    result = tinyusb_driver_install(&usb);
    device_started = result == ESP_OK;
    ESP_LOGI(kTag, "USB transfer init: %s", esp_err_to_name(result));
    return result;
}

bool Failed() { return failed.load(); }

bool HostReleased() {
    // A completed APP ownership transition is authoritative. Hosts can also
    // deconfigure USB after eject; tud_mounted() must not veto that release.
    // Finish still stops USB and checks deferred writes before any app restart.
    return device_started && app_owned && !failed;
}

esp_err_t Finish() {
    if (device_started) {
        // Cancelling before the first USB configuration is also safe: no host
        // has accessed this LUN yet. Otherwise require completed owner handoff.
        if ((ever_attached || tud_mounted()) && !HostReleased())
            return ESP_ERR_INVALID_STATE;
        // First stop USB servicing, so no callback can touch a deleted LUN.
        auto result = tinyusb_driver_uninstall();
        if (result != ESP_OK)
            return result;
        device_started = false;
    }
    if (storage) {
        // The component refuses deletion with pending deferred writes. Never
        // reboot/clear the persistent mode when that check fails.
        auto result = tinyusb_msc_delete_storage(storage);
        if (result != ESP_OK)
            return result;
        storage = nullptr;
    }
    if (msc_initialized) {
        auto result = tinyusb_msc_uninstall_driver();
        if (result != ESP_OK)
            return result;
        msc_initialized = false;
    }
    if (host_initialized) {
        auto result = sdmmc_host_deinit();
        if (result != ESP_OK)
            return result;
        host_initialized = false;
    }
    return ESP_OK;
}
}  // namespace LeleUsb
#endif
