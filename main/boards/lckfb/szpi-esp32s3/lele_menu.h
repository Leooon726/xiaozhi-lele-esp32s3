#pragma once
#include "sdkconfig.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <esp_timer.h>
#include <lvgl.h>
#include <atomic>
#include <memory>
#include "assistant_tap.h"
#include "music_browser.h"
#include "wifi_upload.h"

class Display;
class Backlight;
class LvglFont;
class LeleLcdDisplay;

class LeleMenu {
public:
    void Configure(Display* display, Backlight* backlight);
    void ConfigureOrientation(LeleLcdDisplay* display) { orientation_display_ = display; }
    void RunTransferBootIfRequested();
    void Start();
    void Toggle();
    bool HandleClick();
    bool IsOpen() const { return open_.load(); }
    bool IsTransferBoot() const { return transfer_boot_; }

private:
    enum class Action {
        None,
        Enter,
        WifiEnter,
        Close,
        Return,
        DisplaySettings,
        Theme,
        Dim,
        Brighten,
        Orientation,
        MainMenu,
        Songs,
        PreviousSongs,
        NextSongs,
        PlaySong,
        SongDetails,
        StopSong
    };
    struct Binding {
        LeleMenu* menu;
        Action action;
        std::string path;
        AssistantTap tap;
        bool released_tap = false;
    };
    Binding bindings_[TfMusic::kBrowserPageSize + 4]{};
    std::atomic<Action> action_{Action::None};
    std::atomic<bool> open_{false};
    std::atomic<bool> poll_queued_{false};
    std::atomic<bool> transfer_boot_{false};
    bool preparing_ = false;
    bool restart_to_transfer_ = false;
    bool usb_ready_ = false;
    LeleWifiUpload wifi_upload_;
    bool wifi_transfer_ = false;
    bool wifi_started_ = false;
    bool wifi_closing_ = false;
    unsigned wifi_saved_ = 0;
    const char* wifi_exit_message_ = "Transfer ended.";
    int64_t prepare_started_ = 0;
    esp_timer_handle_t timer_ = nullptr;
    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* message_ = nullptr;
    lv_obj_t* assistant_region_ = nullptr;
    AssistantTap assistant_tap_;
    std::atomic<bool> assistant_tap_pending_{false};
    Display* display_ = nullptr;
    Backlight* backlight_ = nullptr;
    LeleLcdDisplay* orientation_display_ = nullptr;
    int brightness_ = 75;
    bool display_settings_ = false;
    bool songs_page_ = false;
    TfMusic::BrowserPage song_page_;
    std::string pending_song_;  // All access under LVGL lock; captures path at click time.
    std::string song_detail_;
    std::shared_ptr<LvglFont> song_font_;
    uint32_t song_polled_at_ = 0;
    int song_status_ = -1;
    void ResetOverlay();  // Caller holds LVGL lock.
    void ShowSongs(size_t offset);
    void PollSongs();
    void PlaySong(bool details);
    void HandleSongTouch(lv_event_t* event, Binding* binding);
    void UpdateSongStatus();
    void Show(const char* text, bool transfer = false);
    void ShowDisplaySettings();
    void ChangeTheme();
    void ChangeBrightness(int delta);
    void ChangeOrientation();
    bool IsDark() const;
    void Message(const char* text);
    void Close();
    void Poll();
    void Enter(bool network = false);
    void PollWifiUpload();
    void Return();
    lv_obj_t* AddButton(const char* label, Action action, unsigned slot, lv_obj_t* parent = nullptr,
                        const std::string& path = {});
    void InitializeAssistantTouch();
    void HandleAssistantTouch(lv_event_t* event);
};
#endif
