// Adapted from jianglixi-oss/xiaozhi-nas-tf-music (MIT); see MUSIC_PORT.md.
#include "sdkconfig.h"
#ifdef CONFIG_LICHUANG_TF_MUSIC

#include <esp_log.h>
#include <esp_timer.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
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
#ifdef CONFIG_LICHUANG_TF_MUSIC_DEBUG
#include <fcntl.h>
#include <unistd.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "music_debug_command.h"
#endif

namespace {
std::mutex card_mutex;
sdmmc_card_t* card = nullptr;
std::vector<std::string> library;
bool scanning = false;
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
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
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
    return esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &card) == ESP_OK;
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
    if (index.io_error) {
        ESP_LOGW(kTag, "Scan had an I/O error; keeping previous index and cache");
        std::lock_guard<std::mutex> lock(card_mutex);
        index_error =
            "SD scan failed; previous index retained. Retry refresh or restart with card inserted.";
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
    }
    ESP_LOGI(kTag, "Scan complete: %u MP3 tracks, cache saved=%d", unsigned(index.paths.size()),
             saved);
    std::lock_guard<std::mutex> lock(card_mutex);
    library = std::move(index.paths);
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
void RunDebugCommand(TfMusic::DebugCommand command) {
    auto& app = Application::GetInstance();
    if (command.action == TfMusic::DebugAction::Stop) {
        app.RequestMusicStop();
        ESP_LOGI("TfMusicDebug", "Stop requested");
        return;
    }
    std::string path;
    {
        std::lock_guard<std::mutex> lock(card_mutex);
        if (command.action == TfMusic::DebugAction::Status) {
            ESP_LOGI("TfMusicDebug", "ready=%d tracks=%u scanning=%d playback=%d state=%d", ready,
                     unsigned(library.size()), scanning, app.GetMusicStatus(),
                     int(app.GetDeviceState()));
            return;
        }
        if (command.action != TfMusic::DebugAction::Play) {
            ESP_LOGW("TfMusicDebug", "Use: music-test play 0..127 | stop | status");
            return;
        }
        if (!ready || command.index >= library.size()) {
            ESP_LOGW("TfMusicDebug", "Index not ready or track out of range");
            return;
        }
        path = library[command.index];
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
    ESP_LOGI("TfMusicDebug", "USB ready: music-test play 0..127 | stop | status");
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

#ifdef CONFIG_LICHUANG_TF_MUSIC_DEBUG
void ToggleLichuangMusicDebug() {
    const int status = Application::GetInstance().GetMusicStatus();
    RunDebugCommand(
        {status == 1 || status == 2 ? TfMusic::DebugAction::Stop : TfMusic::DebugAction::Play, 0});
}
#endif

void InitializeLichuangMusicTools() {
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
                if (!TfMusic::Matches(path.substr(8), query))
                    continue;
                if (cJSON_GetArraySize(files) >= 20 ||
                    result_bytes + path.size() * 2 + 140 > 6000) {
                    truncated = true;
                    break;
                }
                auto* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "name", path.substr(8).c_str());
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
            if (!ready || !TfMusic::ValidPath(path) ||
                std::find(library.begin(), library.end(), path) == library.end())
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
    StartScanLocked(false, true);
}
#endif
