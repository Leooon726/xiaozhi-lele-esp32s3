// Adapted from jianglixi-oss/xiaozhi-nas-tf-music (MIT); see MUSIC_PORT.md.
#include "sdkconfig.h"
#ifdef CONFIG_LICHUANG_TF_MUSIC

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <mutex>
#include <string>
#include <vector>
#include "application.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "mcp_server.h"
#include "music_index_cache.h"
#include "music_library.h"
#include "music_sd_clock.h"
#include "music_storage.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <nvs.h>
#endif
#ifdef CONFIG_LICHUANG_TF_MUSIC_DEBUG
#include <fcntl.h>
#include <psa/crypto.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <new>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "music_debug_command.h"
#include "music_read_check.h"
#endif

namespace {
#ifdef CONFIG_LICHUANG_USB_STORAGE
bool IndexNeedsRefresh() {
    nvs_handle_t handle;
    const auto opened = nvs_open("lele_files", NVS_READONLY, &handle);
    if (opened != ESP_OK)
        return opened != ESP_ERR_NVS_NOT_FOUND;
    uint8_t dirty = 0;
    const auto result = nvs_get_u8(handle, "dirty", &dirty);
    nvs_close(handle);
    return result == ESP_OK ? dirty != 0 : result != ESP_ERR_NVS_NOT_FOUND;
}

bool WriteIndexDirty(bool dirty) {
    nvs_handle_t handle;
    if (nvs_open("lele_files", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    const bool saved =
        nvs_set_u8(handle, "dirty", dirty ? 1 : 0) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return saved;
}
#endif
std::mutex card_mutex;
sdmmc_card_t* card = nullptr;
TfMusic::MusicPaths library;
uint32_t library_revision = 0;  // Access only under card_mutex, including UI snapshots.
bool scanning = false;
bool storage_pending = false;
bool debug_verify_active = false;  // Protected by card_mutex; false in non-debug builds.
bool ready = false;
bool library_truncated = false;
bool cache_saved = false;
std::string index_source = "none";
std::string index_error;
std::atomic<bool> manual_refresh{false};
constexpr const char* kMusicRoot = "/sdcard";
constexpr const char* kCacheFiles[] = {"/sdcard/.lele-music-index-a.bin",
                                       "/sdcard/.lele-music-index-b.bin"};
// Only the single indexing worker accesses these after initialization.
int active_cache_slot = -1;
uint32_t cache_generation = 0;
constexpr const char* kTag = "TfMusicIndex";

bool MountCard() {
    if (card)
        return true;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = TfMusic::kSdClockKhz;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    // Lichuang Shizhanpai S3: SDMMC 1-bit, independent of LCD SPI and I2S.
    slot.clk = GPIO_NUM_47;
    slot.cmd = GPIO_NUM_48;
    slot.d0 = GPIO_NUM_21;
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;  // Never format a user's music card.
    mount.max_files = 8;
    mount.allocation_unit_size = 16 * 1024;
    const auto result = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &card);
    if (result == ESP_OK)
        ESP_LOGI(kTag, "SD clock: requested=%d actual=%d kHz", TfMusic::kSdClockKhz,
                 card->real_freq_khz);
    return result == ESP_OK;
}

// All filesystem I/O stays in this worker. MCP searches keep using the old index.
void RunScan(bool load_cache) {
    if (!MountCard()) {
        std::lock_guard<std::mutex> lock(card_mutex);
        index_error = "SD mount failed; insert a FAT32 card and retry. Card was not formatted.";
        scanning = false;
        manual_refresh = false;
        return;
    }
    if (load_cache) {
        TfMusic::Index selected;
        active_cache_slot = TfMusic::LoadLatestCache(kCacheFiles[0], kCacheFiles[1], kMusicRoot,
                                                     selected, cache_generation);
        if (active_cache_slot >= 0) {
            ESP_LOGI(kTag, "Loaded cached index: %u MP3 tracks, generation %lu",
                     unsigned(selected.paths.size()), static_cast<unsigned long>(cache_generation));
            std::lock_guard<std::mutex> lock(card_mutex);
            library = std::move(selected.paths);
            ++library_revision;
            library_truncated = selected.truncated;
            ready = true;
            cache_saved = true;
            index_source = "cache";
        }
    }

    TfMusic::IdleScanPolicy policy;
    unsigned checkpoints = 0;
    auto checkpoint = [&]() {
        while (true) {
            const auto state = Application::GetInstance().GetDeviceState();
            const bool idle = state == kDeviceStateIdle &&
                              Application::GetInstance().GetAudioService().IsPlaybackIdle();
            const bool safe_state =
                idle || state == kDeviceStateListening || state == kDeviceStateSpeaking ||
                state == kDeviceStateConnecting || state == kDeviceStateNotifying;
            const auto now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            const bool allowed = policy.CanRun(manual_refresh.load(), idle, now);
            if (safe_state && allowed)
                break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        // Manual scans also yield, and remain below all audio tasks in priority.
        if (++checkpoints % 8 == 0)
            vTaskDelay(pdMS_TO_TICKS(20));
    };
    TfMusic::Index index;
    TfMusic::Scan(kMusicRoot, kMusicRoot, 0, index, checkpoint);
    std::sort(index.paths.begin(), index.paths.end());
    if (index.io_error || index.memory_error) {
        ESP_LOGW(kTag, "Scan failed (I/O=%d memory=%d); keeping previous index and cache",
                 index.io_error, index.memory_error);
        std::lock_guard<std::mutex> lock(card_mutex);
        index_error = index.memory_error ? "Insufficient PSRAM; previous index retained."
                                         : "SD scan failed; previous index retained. Retry refresh "
                                           "or restart with card inserted.";
        scanning = false;
        manual_refresh = false;
        return;
    }
    const int next_slot = active_cache_slot == 0 ? 1 : 0;
    const uint32_t next_generation = cache_generation + 1;
    const bool saved =
        TfMusic::SaveCache(kCacheFiles[next_slot], kMusicRoot, index, next_generation, checkpoint);
    if (saved) {
        active_cache_slot = next_slot;
        cache_generation = next_generation;
#ifdef CONFIG_LICHUANG_USB_STORAGE
        if (IndexNeedsRefresh() && !WriteIndexDirty(false))
            ESP_LOGW(kTag, "Index saved; could not clear pending rebuild marker");
#endif
    }
    ESP_LOGI(kTag, "Scan complete: %u MP3 tracks, cache saved=%d", unsigned(index.paths.size()),
             saved);
    ESP_LOGI(kTag, "Index PSRAM bytes=%u, free PSRAM=%u, largest PSRAM=%u",
             unsigned(index.paths.storage_bytes()),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    std::lock_guard<std::mutex> lock(card_mutex);
    library = std::move(index.paths);
    ++library_revision;
    library_truncated = index.truncated;
    ready = true;
    cache_saved = saved;
    index_source = "scan";
    index_error =
        saved ? "" : "Index ready in RAM; cache save failed, previous disk cache retained.";
    scanning = false;
    manual_refresh = false;
}

// Caller holds card_mutex. Explicit refresh promotes an already waiting scan.
bool StartScanLocked(bool manual = false, bool load_cache = false) {
    if (storage_pending || debug_verify_active)
        return false;
    if (manual)
        manual_refresh = true;
    if (scanning)
        return true;
    scanning = true;
    index_error.clear();
    if (xTaskCreate(
            [](void* arg) {
                RunScan(arg != nullptr);
                // RunScan returns before deletion so C++ destructors release its buffers.
                vTaskDelete(nullptr);
            },
            "tf_index", 8192, load_cache ? reinterpret_cast<void*>(1) : nullptr, 1,
            nullptr) != pdPASS) {
        scanning = false;
        manual_refresh = false;
        index_error = "Unable to start TF indexing task";
        return false;
    }
    return true;
}
#ifdef CONFIG_LICHUANG_TF_MUSIC_DEBUG
std::atomic<bool> debug_verify_cancel{false};

struct ReadCheckJob {
    std::string path;
    unsigned index;
    bool internal = false;
};

bool CheckReadBufferMemory(uint8_t* buffer) {
    // Fixed public test vector, independent of SD contents. Not a full RAM test.
    constexpr size_t size = 4096;
    for (size_t i = 0; i < size; ++i)
        buffer[i] = static_cast<uint8_t>(i);
    constexpr std::array<uint8_t, 32> expected = {0xc8, 0xf5, 0xd0, 0x34, 0x1d, 0x54, 0xd9, 0x51,
                                                  0xa7, 0x1b, 0x13, 0x6e, 0x6e, 0x2a, 0xfc, 0xb1,
                                                  0x4d, 0x11, 0xed, 0x84, 0x89, 0xa7, 0xae, 0x12,
                                                  0x6a, 0x8f, 0xee, 0x0d, 0xf6, 0xec, 0xf1, 0x93};
    std::array<uint8_t, 32> digest{};
    size_t length = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, buffer, size, digest.data(), digest.size(), &length) ==
               PSA_SUCCESS &&
           length == digest.size() && digest == expected;
}

void RunReadCheck(const ReadCheckJob& job) {
    constexpr size_t max_bytes = 16 * 1024 * 1024;
    constexpr size_t buffer_size = 4096;
    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> buffer(
        static_cast<uint8_t*>(heap_caps_malloc(
            buffer_size,
            (job.internal ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM) | MALLOC_CAP_8BIT)),
        heap_caps_free);
    if (!buffer || psa_crypto_init() != PSA_SUCCESS) {
        ESP_LOGE("TfMusicVerify", "Unable to initialize read check");
        return;
    }
    const int64_t deadline = esp_timer_get_time() + 90000000;
    auto cancelled = [&]() {
        std::lock_guard<std::mutex> lock(card_mutex);
        return debug_verify_cancel.load() || storage_pending ||
               Application::GetInstance().GetDeviceState() != kDeviceStateIdle ||
               esp_timer_get_time() >= deadline;
    };
    std::array<uint8_t, 32> first_digest{};
    size_t first_bytes = 0;
    bool all_equal = true;
    ESP_LOGI("TfMusicVerify", "Start index=%u passes=3 max_bytes=%u buffer=%s", job.index,
             unsigned(max_bytes), job.internal ? "internal" : "PSRAM");
    for (unsigned pass = 1; pass <= 3; ++pass) {
        if (cancelled()) {
            ESP_LOGW("TfMusicVerify", "Cancelled before pass=%u", pass);
            return;
        }
        if (!CheckReadBufferMemory(buffer.get())) {
            ESP_LOGE("TfMusicVerify", "Known-pattern SHA check failed before pass=%u buffer=%s",
                     pass, job.internal ? "internal" : "PSRAM");
            return;
        }
        ESP_LOGI("TfMusicVerify", "Known-pattern SHA check passed before pass=%u", pass);
        const int64_t started = esp_timer_get_time();
        std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(job.path.c_str(), "rb"),
                                                           std::fclose);
        if (!file) {
            ESP_LOGE("TfMusicVerify", "Open failed pass=%u errno=%d", pass, errno);
            return;
        }
        // Reopen every pass and avoid stdio read-ahead. Only SD is read.
        std::setvbuf(file.get(), nullptr, _IONBF, 0);
        psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
        if (psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
            psa_hash_abort(&hash);
            ESP_LOGE("TfMusicVerify", "SHA setup failed pass=%u", pass);
            return;
        }
        unsigned chunks = 0;
        const auto result = TfMusic::CheckReadStream(
            [&](uint8_t* data, size_t size) {
                if (++chunks % 16 == 0)
                    vTaskDelay(1);
                const size_t count = std::fread(data, 1, size, file.get());
                return std::ferror(file.get()) ? -1 : static_cast<int>(count);
            },
            [&](const uint8_t* data, size_t size) {
                return psa_hash_update(&hash, data, size) == PSA_SUCCESS;
            },
            cancelled, buffer.get(), buffer_size, max_bytes);
        std::array<uint8_t, 32> digest{};
        size_t length = 0;
        const bool complete =
            result.status == TfMusic::ReadCheckStatus::Complete &&
            psa_hash_finish(&hash, digest.data(), digest.size(), &length) == PSA_SUCCESS &&
            length == digest.size();
        psa_hash_abort(&hash);
        const int close_error = std::fclose(file.release());
        if (!complete || close_error != 0) {
            ESP_LOGE("TfMusicVerify", "Incomplete pass=%u status=%d bytes=%u close=%d", pass,
                     int(result.status), unsigned(result.bytes), close_error);
            return;
        }
        if (pass == 1) {
            first_digest = digest;
            first_bytes = result.bytes;
        }
        const bool equal = digest == first_digest && result.bytes == first_bytes;
        all_equal = all_equal && equal;
        char hex[65];
        constexpr char digits[] = "0123456789abcdef";
        for (size_t i = 0; i < digest.size(); ++i) {
            hex[2 * i] = digits[digest[i] >> 4];
            hex[2 * i + 1] = digits[digest[i] & 15];
        }
        hex[64] = 0;
        ESP_LOGI("TfMusicVerify", "pass=%u bytes=%u sha256=%s matches_first=%d elapsed_ms=%lu",
                 pass, unsigned(result.bytes), hex, equal,
                 static_cast<unsigned long>((esp_timer_get_time() - started) / 1000));
    }
    if (!CheckReadBufferMemory(buffer.get())) {
        ESP_LOGE("TfMusicVerify", "Known-pattern SHA check failed after file reads");
        return;
    }
    ESP_LOGI("TfMusicVerify", "Done index=%u passes=3 all_equal=%d buffer=%s memory_checks=4",
             job.index, all_equal, job.internal ? "internal" : "PSRAM");
}

void StartReadCheck(unsigned index, bool internal = false) {
    Application::GetInstance().Schedule([index, internal]() {
        auto& app = Application::GetInstance();
        std::unique_ptr<ReadCheckJob> job;
        {
            std::lock_guard<std::mutex> lock(card_mutex);
            if (storage_pending || debug_verify_active || scanning || !ready ||
                index >= library.size() || app.GetDeviceState() != kDeviceStateIdle ||
                !app.GetAudioService().IsPlaybackIdle()) {
                ESP_LOGW("TfMusicVerify",
                         "Busy or index not ready; retry when idle and scanning=0");
                return;
            }
            job.reset(new (std::nothrow)
                          ReadCheckJob{std::string(library[index].view()), index, internal});
            if (!job) {
                ESP_LOGE("TfMusicVerify", "Unable to allocate job");
                return;
            }
            debug_verify_active = true;
            debug_verify_cancel = false;
        }
        app.SetMusicStorageBlocked(true);
        auto* context = job.release();
        const auto created = xTaskCreateWithCaps(
            [](void* arg) {
                {
                    std::unique_ptr<ReadCheckJob> owned(static_cast<ReadCheckJob*>(arg));
                    RunReadCheck(*owned);
                }  // File, hash, buffer and path are released before ownership is cleared.
                Application::GetInstance().Schedule([]() {
                    bool transfer;
                    {
                        std::lock_guard<std::mutex> lock(card_mutex);
                        debug_verify_active = false;
                        transfer = storage_pending;
                    }
                    if (!transfer)
                        Application::GetInstance().SetMusicStorageBlocked(false);
                });
                vTaskDeleteWithCaps(nullptr);
            },
            "tf_verify", 6144, context, 1, nullptr, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (created != pdPASS) {
            delete context;
            {
                std::lock_guard<std::mutex> lock(card_mutex);
                debug_verify_active = false;
            }
            app.SetMusicStorageBlocked(false);
            ESP_LOGE("TfMusicVerify", "Unable to create worker");
        }
    });
}

void RunDebugCommand(TfMusic::DebugCommand command) {
    auto& app = Application::GetInstance();
    if (command.action == TfMusic::DebugAction::Stop) {
        debug_verify_cancel = true;
        app.RequestMusicStop();
        ESP_LOGI("TfMusicDebug", "Stop requested");
        return;
    }
    if (command.action == TfMusic::DebugAction::Verify ||
        command.action == TfMusic::DebugAction::VerifyInternal) {
        StartReadCheck(command.index, command.action == TfMusic::DebugAction::VerifyInternal);
        return;
    }
    std::string path;
    {
        std::lock_guard<std::mutex> lock(card_mutex);
        if (command.action == TfMusic::DebugAction::Status) {
            ESP_LOGI("TfMusicDebug",
                     "ready=%d tracks=%u scanning=%d playback=%d state=%d verifying=%d", ready,
                     unsigned(library.size()), scanning, app.GetMusicStatus(),
                     int(app.GetDeviceState()), debug_verify_active);
            return;
        }
        if (command.action != TfMusic::DebugAction::Play) {
            ESP_LOGW("TfMusicDebug", "Use: music-test play/verify 0..%u | stop | status",
                     unsigned(TfMusic::kMaxTracks - 1));
            return;
        }
        if (storage_pending || debug_verify_active || !ready || command.index >= library.size()) {
            ESP_LOGW("TfMusicDebug", "Index not ready or track out of range");
            return;
        }
        path = library[command.index].view();
    }
    if (app.GetDeviceState() != kDeviceStateIdle) {
        ESP_LOGW("TfMusicDebug", "Device busy; stop playback or wait for idle first");
        return;
    }
    ESP_LOGI("TfMusicDebug", "Play index=%u path=%s", command.index, path.c_str());
    app.RequestMusic(std::move(path));
}

void DebugSerialTask(void*) {
    // IDF registers this VFS automatically for a primary USB console only.
    // This board uses UART primary + USB secondary, so register its input here.
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    const auto registered = usb_serial_jtag_vfs_register();
    if (registered != ESP_OK) {
        ESP_LOGE("TfMusicDebug", "Cannot register USB input: %s", esp_err_to_name(registered));
        vTaskDelete(nullptr);
        return;
    }
#endif
    // IDF 6.1 nonblocking VFS reads use the driver's RX ring-buffer count.
    // Polling console output alone does not initialize that input driver.
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {};
        config.rx_buffer_size = 256;
        config.tx_buffer_size = 1024;
        const auto installed = usb_serial_jtag_driver_install(&config);
        if (installed != ESP_OK) {
            ESP_LOGE("TfMusicDebug", "Cannot install USB input driver: %s",
                     esp_err_to_name(installed));
            vTaskDelete(nullptr);
            return;
        }
    }
    usb_serial_jtag_vfs_use_driver();
    const int fd = open("/dev/usbserjtag", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        ESP_LOGE("TfMusicDebug", "Cannot open USB diagnostic input");
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI("TfMusicDebug", "USB ready: music-test play/verify 0..%u | stop | status",
             unsigned(TfMusic::kMaxTracks - 1));
    char line[64];
    size_t used = 0;
    bool overflow = false;
    while (true) {
        char bytes[32];
        const auto count = read(fd, bytes, sizeof(bytes));
        for (ssize_t i = 0; i < count; ++i) {
            const char c = bytes[i];
            if (c == '\r')
                continue;
            if (c == '\n') {
                if (!overflow && used)
                    RunDebugCommand(TfMusic::ParseDebugCommand({line, used}));
                else if (overflow)
                    ESP_LOGW("TfMusicDebug", "Command too long; discarded");
                used = 0;
                overflow = false;
            } else if (!overflow) {
                if (used == sizeof(line))
                    overflow = true;
                else
                    line[used++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif
}  // namespace

TfMusic::BrowserPage GetLichuangMusicPage(size_t offset) {
    std::lock_guard<std::mutex> lock(card_mutex);
    auto page = TfMusic::CopyBrowserPage(library, offset, library_revision);
    page.ready = ready;
    page.scanning = scanning;
    page.truncated = library_truncated;
    page.blocked = storage_pending || debug_verify_active;
    page.error = index_error;
    return page;
}

bool RequestLichuangMusicPath(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(card_mutex);
        if (storage_pending || debug_verify_active || !ready || !TfMusic::ValidPath(path) ||
            !SdAudio::Playable(path) || !TfMusic::HasBrowserPath(library, path))
            return false;
    }
    Application::GetInstance().RequestMusic(path);
    return true;
}

#ifdef CONFIG_LICHUANG_TF_MUSIC_DEBUG
void ToggleLichuangMusicDebug() {
    const int status = Application::GetInstance().GetMusicStatus();
    RunDebugCommand(
        {status == 1 || status == 2 ? TfMusic::DebugAction::Stop : TfMusic::DebugAction::Play, 0});
}
#endif

void BeginLichuangStorageTransfer() {
    std::lock_guard<std::mutex> lock(card_mutex);
    storage_pending = true;
    // Complete any in-flight refresh without the 10-second idle wait.
    manual_refresh = true;
}

void CancelLichuangStorageTransfer() {
    std::lock_guard<std::mutex> lock(card_mutex);
    storage_pending = false;
    manual_refresh = false;
}

bool IsLichuangStorageQuiet() {
    std::lock_guard<std::mutex> lock(card_mutex);
    return storage_pending && !scanning && !debug_verify_active;
}

bool IsLichuangStorageReadyForUpload() {
    std::lock_guard<std::mutex> lock(card_mutex);
    return storage_pending && !scanning && !debug_verify_active && card != nullptr;
}

#ifdef CONFIG_LICHUANG_USB_STORAGE
bool MarkLichuangStorageDirty() { return WriteIndexDirty(true); }
#endif

bool FinishLichuangStorageUpload() {
    std::lock_guard<std::mutex> lock(card_mutex);
    if (!storage_pending || scanning || debug_verify_active)
        return false;
    // Rename/delete can invalidate existing paths. Do not replay a stale RAM
    // index while the new scan (or a failed task allocation) is outstanding.
    ready = false;
    ++library_revision;
    storage_pending = false;
    return StartScanLocked(true);
}

bool UnmountLichuangMusicCard() {
    std::lock_guard<std::mutex> lock(card_mutex);
    if (!storage_pending || scanning || debug_verify_active)
        return false;
    // IDF may free the card before returning a VFS unregister error. Never
    // retain that pointer, or resume ordinary I/O after a partial unmount.
    auto* previous = card;
    card = nullptr;
    ready = false;
    return !previous || esp_vfs_fat_sdcard_unmount(kMusicRoot, previous) == ESP_OK;
}

void InitializeLichuangMusicTools(bool refresh_immediately) {
#ifdef CONFIG_LICHUANG_USB_STORAGE
    refresh_immediately = refresh_immediately || IndexNeedsRefresh();
#endif
#ifdef CONFIG_LICHUANG_TF_MUSIC_DEBUG
    if (xTaskCreate(DebugSerialTask, "music_debug", 4096, nullptr, 1, nullptr) != pdPASS)
        ESP_LOGE("TfMusicDebug", "Cannot create USB diagnostic task");
#endif
    auto& mcp = McpServer::GetInstance();
    mcp.AddTool("self.music.tf_refresh",
                "Immediately rebuild the TF music index and save it on the card. "
                "Use when the user asks to refresh/rescan/rebuild after changing music files.",
                PropertyList(), [](const PropertyList&) -> ToolResult {
                    std::lock_guard<std::mutex> lock(card_mutex);
                    if (!StartScanLocked(true))
                        return std::unexpected("Unable to start TF scan worker");
                    return std::string(
                        "Immediate TF index rebuild requested. Old index stays usable until "
                        "completion; check self.music.status.");
                });
    mcp.AddTool("self.music.status",
                "Get music playback and TF indexing/cache status. A requested rebuild may still be "
                "scanning.",
                PropertyList(), [](const PropertyList&) -> ReturnValue {
                    static const char* names[] = {"idle",      "queued", "playing",
                                                  "completed", "failed", "stopped"};
                    int status = Application::GetInstance().GetMusicStatus();
                    std::lock_guard<std::mutex> lock(card_mutex);
                    auto* result = cJSON_CreateObject();
                    cJSON_AddStringToObject(result, "playback",
                                            names[status >= 0 && status < 6 ? status : 0]);
                    cJSON_AddBoolToObject(result, "index_ready", ready);
                    cJSON_AddBoolToObject(result, "index_updating", scanning);
                    cJSON_AddBoolToObject(result, "manual_rebuild", manual_refresh.load());
                    cJSON_AddBoolToObject(result, "cache_saved", cache_saved);
                    cJSON_AddBoolToObject(result, "truncated", library_truncated);
                    cJSON_AddNumberToObject(result, "tracks", library.size());
                    cJSON_AddNumberToObject(result, "max_tracks", TfMusic::kMaxTracks);
                    cJSON_AddNumberToObject(result, "index_storage_bytes", library.storage_bytes());
                    cJSON_AddStringToObject(result, "index_source", index_source.c_str());
                    cJSON_AddStringToObject(result, "index_error", index_error.c_str());
                    return result;
                });
    mcp.AddTool(
        "self.music.tf_search",
        "Search MP3 filenames across the whole TF card, including root and nested folders. "
        "Only .mp3 files are indexed (case-insensitive). Returns up to 20 files. "
        "Use an empty query to browse. TF voice search needs the cloud.",
        PropertyList({Property("query", kPropertyTypeString, std::string()).SetMaxLength(120)}),
        [](const PropertyList& p) -> ToolResult {
            std::lock_guard<std::mutex> lock(card_mutex);
            if (!ready) {
                if (!StartScanLocked(true))
                    return std::unexpected("Unable to start TF scan worker");
                return std::string(
                    "TF not indexed. Immediate scan requested; retry in a few seconds. Insert "
                    "FAT32 "
                    "card if absent.");
            }
            auto* result = cJSON_CreateObject();
            auto* files = cJSON_AddArrayToObject(result, "files");
            bool truncated = library_truncated;
            const auto& query = p["query"].value<std::string>();
            size_t result_bytes = 0;
            for (const auto& path : library) {
                if (!TfMusic::Matches(path.view().substr(8), query))
                    continue;
                if (cJSON_GetArraySize(files) >= 20 ||
                    result_bytes + path.size() * 2 + 140 > 6000) {
                    truncated = true;
                    break;
                }
                auto* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "name", path.c_str() + 8);
                cJSON_AddStringToObject(item, "path", path.c_str());
                cJSON_AddItemToArray(files, item);
                result_bytes += path.size() * 2 + 140;
            }
            cJSON_AddBoolToObject(result, "truncated", truncated);
            cJSON_AddBoolToObject(result, "index_updating", scanning);
            cJSON_AddStringToObject(result, "index_source", index_source.c_str());
            cJSON_AddStringToObject(result, "index_error", index_error.c_str());
            return result;
        });
    mcp.AddTool(
        "self.music.tf_play", "Play an MP3 path returned by self.music.tf_search.",
        PropertyList({Property("path", kPropertyTypeString).SetMaxLength(240)}),
        [](const PropertyList& p) -> ToolResult {
            std::lock_guard<std::mutex> lock(card_mutex);
            auto path = p["path"].value<std::string>();
            if (storage_pending || debug_verify_active || !ready || !TfMusic::ValidPath(path) ||
                std::find_if(library.begin(), library.end(), [&](const auto& item) {
                    return item.view() == path;
                }) == library.end())
                return std::unexpected("TF MP3 file not indexed; search the TF library first");
            if (!SdAudio::Playable(path))
                return std::unexpected(
                    "Audio file indexed, but this firmware currently decodes MP3 only. Choose a "
                    "result with playable=true.");
            Application::GetInstance().RequestMusic(std::move(path));
            return std::string("TF playback requested.");
        });
    mcp.AddTool("self.music.stop", "Stop TF music and cancel any queued music request.",
                PropertyList(), [](const PropertyList&) -> ReturnValue {
                    Application::GetInstance().RequestMusicStop();
                    return std::string("Stop requested.");
                });
    Application::GetInstance().SetMusicFailureCallback([]() {
        std::lock_guard<std::mutex> lock(card_mutex);
        StartScanLocked();
    });
    std::lock_guard<std::mutex> lock(card_mutex);
    StartScanLocked(refresh_immediately, !refresh_immediately);
}
#endif
