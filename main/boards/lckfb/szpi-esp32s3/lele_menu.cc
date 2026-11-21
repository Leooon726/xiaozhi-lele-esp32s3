#include "lele_menu.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstdio>
#include "application.h"
#include "assistant_touch_region.h"
#include "backlight.h"
#include "display.h"
#include "lele_display.h"
#include "lvgl_theme.h"
#include "music_storage.h"
#include "settings.h"
#include "song_list_layout.h"
#include "usb_transfer.h"
#include "wifi_manager.h"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

void LeleMenu::Configure(Display* display, Backlight* backlight) {
    display_ = display;
    backlight_ = backlight;
}

bool LeleMenu::IsDark() const {
    auto* theme = display_ ? display_->GetTheme() : nullptr;
    return theme && theme->name() == "dark";
}

lv_obj_t* LeleMenu::AddButton(const char* label, Action action, unsigned slot, lv_obj_t* parent,
                              const std::string& path) {
    auto* button = CreateLeleButton(parent ? parent : overlay_, label, IsDark());
    bindings_[slot] = {this, action, path};
    if (action == Action::PlaySong) {
        lv_obj_add_event_cb(
            button,
            [](lv_event_t* event) {
                auto* binding = static_cast<Binding*>(lv_event_get_user_data(event));
                binding->menu->HandleSongTouch(event, binding);
            },
            LV_EVENT_ALL, &bindings_[slot]);
        return button;
    }
    lv_obj_add_event_cb(
        button,
        [](lv_event_t* event) {
            auto* binding = static_cast<Binding*>(lv_event_get_user_data(event));
            binding->menu->action_.store(binding->action);
        },
        LV_EVENT_CLICKED, &bindings_[slot]);
    return button;
}

void LeleMenu::HandleSongTouch(lv_event_t* event, Binding* binding) {
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED) {
        const bool details = code == LV_EVENT_LONG_PRESSED;
        if ((details && binding->tap.Stationary()) || (!details && binding->released_tap)) {
            pending_song_ = binding->path;
            action_ = details ? Action::SongDetails : Action::PlaySong;
        }
        binding->released_tap = false;
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED &&
        code != LV_EVENT_PRESS_LOST)
        return;
    auto* input = lv_indev_active();
    if (!input) {
        binding->tap.Cancel();
        return;
    }
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        binding->released_tap = false;
        binding->tap.Begin(point.x, point.y, lv_tick_get(), true);
    } else if (code == LV_EVENT_PRESSING) {
        binding->tap.Move(point.x, point.y);
    } else if (code == LV_EVENT_RELEASED) {
        binding->released_tap = binding->tap.End(point.x, point.y, lv_tick_get(), true);
    } else {
        binding->tap.Cancel();
        binding->released_tap = false;
    }
}

void LeleMenu::ResetOverlay() {
    assistant_tap_.Cancel();
    assistant_tap_pending_ = false;
    // Rebuild only on page navigation, never from a touch callback. Input
    // callbacks merely queue an action, so deleted controls cannot be reused.
    if (overlay_)
        lv_obj_delete(overlay_);
    overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay_, lv_pct(100), lv_pct(100));
    lv_obj_center(overlay_);
    lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(overlay_, IsDark() ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(overlay_, IsDark() ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_text_font(overlay_, &BUILTIN_TEXT_FONT, 0);
    lv_obj_set_style_pad_all(overlay_, 6, 0);
    lv_obj_set_style_pad_row(overlay_, 4, 0);
    lv_obj_set_flex_flow(overlay_, LV_FLEX_FLOW_COLUMN);
}

void LeleMenu::Show(const char* text, bool transfer) {
    if (!lvgl_port_lock(1000))
        return;
    ResetOverlay();
    {
        auto* title = lv_label_create(overlay_);
        lv_label_set_text(title, transfer         ? "USB File Transfer"
                                 : wifi_transfer_ ? "Wi-Fi File Transfer"
                                                  : (display_settings_ ? "Display" : "Lele Menu"));
        message_ = lv_label_create(overlay_);
        lv_obj_set_width(message_, lv_pct(100));
        lv_obj_set_height(message_, display_settings_ ? 28 : 48);
        lv_label_set_long_mode(message_, LV_LABEL_LONG_WRAP);
        if (wifi_transfer_) {
            lv_obj_set_height(message_, 0);
            lv_obj_set_flex_grow(message_, 1);
            lv_obj_set_style_text_font(message_, LV_FONT_DEFAULT, 0);
            AddButton("End Wi-Fi Transfer", Action::Close, 0);
        } else if (transfer) {
            AddButton("Return to Assistant", Action::Return, 0);
        } else if (display_settings_) {
            AddButton(IsDark() ? "Theme: Dark" : "Theme: Light", Action::Theme, 0);
#ifdef CONFIG_USE_DEFAULT_MESSAGE_STYLE
            if (orientation_display_) {
                AddButton(orientation_display_->IsPortrait() ? "Orientation: Portrait"
                                                             : "Orientation: Landscape",
                          Action::Orientation, 4);
            }
#endif
            auto* brightness = CreateSongListFooter(overlay_);
            lv_obj_set_height(brightness, 32);
            auto* dim = AddButton("Dim -", Action::Dim, 1, brightness);
            auto* brighten = AddButton("Bright +", Action::Brighten, 2, brightness);
            for (auto* button : {dim, brighten}) {
                lv_obj_set_width(button, 0);
                lv_obj_set_flex_grow(button, 1);
            }
            AddButton("Back", Action::MainMenu, 3);
        } else {
            AddButton("USB File Transfer", Action::Enter, 0);
            AddButton("Wi-Fi File Transfer", Action::WifiEnter, 4);
            auto* row = CreateSongListFooter(overlay_);
            lv_obj_set_height(row, 32);
            auto* display = AddButton("Display", Action::DisplaySettings, 1, row);
            auto* songs = AddButton("Songs", Action::Songs, 2, row);
            for (auto* button : {display, songs}) {
                lv_obj_set_width(button, 0);
                lv_obj_set_flex_grow(button, 1);
            }
            AddButton("Back", Action::Close, 3);
        }
    }
    lv_label_set_text(message_, text);
    lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    open_ = true;
    lvgl_port_unlock();
}

void LeleMenu::ShowDisplaySettings() {
    if (preparing_ || transfer_boot_ || restart_to_transfer_ || wifi_transfer_)
        return;
    display_settings_ = true;
    songs_page_ = false;
    Settings settings("display", false);
    brightness_ = std::clamp<int32_t>(settings.GetInt("brightness", 75), 5, 100);
    char text[40];
    snprintf(text, sizeof(text), "Brightness: %d%%", brightness_);
    Show(text);
}

void LeleMenu::ShowSongs(size_t offset) {
    if (preparing_ || transfer_boot_ || restart_to_transfer_ || wifi_transfer_)
        return;
    auto page = GetLichuangMusicPage(offset);  // Releases card mutex before LVGL lock.
    if (!lvgl_port_lock(1000))
        return;
    if (!songs_page_ || page.offset != song_page_.offset || page.revision != song_page_.revision)
        song_detail_.clear();
    ResetOverlay();
    songs_page_ = true;
    display_settings_ = false;
    song_page_ = std::move(page);
    song_font_ = display_ && display_->GetTheme() ? display_->GetTheme()->GetTextFont() : nullptr;
    lv_obj_set_style_border_width(overlay_, 0, 0);
    // One header only: status/details share it instead of taking a song row.
    message_ = lv_label_create(overlay_);
    lv_obj_set_size(message_, lv_pct(100), LV_SIZE_CONTENT);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    if (song_font_)
        lv_obj_set_style_text_font(message_, song_font_->font(), 0);
    auto* rows = CreateSongListRows(overlay_);
    for (size_t row = 0; row < song_page_.count; ++row) {
        const auto& path = song_page_.paths[row];
        const auto name =
            std::to_string(song_page_.offset + row + 1) + ". " + TfMusic::BrowserName(path);
        auto* button = AddButton(name.c_str(), Action::PlaySong, row, rows, path);
        ConfigureSongListButton(button, song_font_ ? song_font_->font() : nullptr);
        if (!song_page_.ready || song_page_.blocked)
            lv_obj_add_state(button, LV_STATE_DISABLED);
    }
    auto* footer = CreateSongListFooter(overlay_);
    const char* names[] = {"Prev", "Next", "Stop", "Back"};
    const Action actions[] = {Action::PreviousSongs, Action::NextSongs, Action::StopSong,
                              Action::MainMenu};
    for (unsigned i = 0; i < 4; ++i) {
        auto* button = AddButton(names[i], actions[i], i + TfMusic::kBrowserPageSize, footer);
        lv_obj_set_width(button, 0);
        lv_obj_set_height(button, 30);
        lv_obj_set_flex_grow(button, 1);
        if ((i == 0 && !song_page_.offset) ||
            (i == 1 && song_page_.offset + song_page_.count >= song_page_.total))
            lv_obj_add_state(button, LV_STATE_DISABLED);
    }
    open_ = true;
    lvgl_port_unlock();
    song_polled_at_ = uint32_t(esp_timer_get_time() / 1000);
    ESP_LOGI("LeleSongs", "Page offset=%u rows=%u total=%u revision=%lu",
             unsigned(song_page_.offset), unsigned(song_page_.count), unsigned(song_page_.total),
             static_cast<unsigned long>(song_page_.revision));
    UpdateSongStatus();
}

void LeleMenu::UpdateSongStatus() {
    song_status_ = Application::GetInstance().GetMusicStatus();
    std::string text;
    if (song_page_.blocked)
        text = "Storage unavailable during transfer";
    else if (!song_page_.ready)
        text = !song_page_.error.empty()
                   ? song_page_.error
                   : (song_page_.scanning ? "Loading SD index..." : "SD index unavailable");
    else if (!song_page_.total)
        text = "No MP3 files indexed";
    else {
        const char* states[] = {"Ready",    "Starting",        "Playing",
                                "Finished", "Playback failed", "Stopped"};
        text = states[std::clamp(song_status_, 0, 5)];
        if (song_page_.scanning)
            text += " | index updating";
        if (song_page_.truncated)
            text += " | limited index";
        if (!song_page_.error.empty())
            text += " | index warning";
        if (!song_detail_.empty())
            text += ": " + song_detail_.substr(8);
    }
    char heading[60];
    const auto size = TfMusic::kBrowserPageSize;
    const size_t pages = std::max(size_t(1), (song_page_.total + size - 1) / size);
    snprintf(heading, sizeof(heading), "Songs %u/%u (%u)", unsigned(song_page_.offset / size + 1),
             unsigned(pages), unsigned(song_page_.total));
    Message((std::string(heading) + (text.empty() ? "" : " | " + text)).c_str());
}

void LeleMenu::PlaySong(bool details) {
    if (!songs_page_ || !IsOpen() || preparing_ || transfer_boot_ || restart_to_transfer_)
        return;
    if (!lvgl_port_lock(1000))
        return;
    std::string path = std::move(pending_song_);
    pending_song_.clear();
    lvgl_port_unlock();
    if (path.empty())
        return;
    song_detail_ = path;
    if (details) {
        UpdateSongStatus();
        return;
    }
    const auto state = Application::GetInstance().GetDeviceState();
    if (state != kDeviceStateIdle && state != kDeviceStateNotifying &&
        state != kDeviceStateListening && state != kDeviceStateSpeaking) {
        Message("Assistant not ready. Please wait until connected.");
        return;
    }
    // The click captured a path, never an index that a refresh can reassign.
    if (!RequestLichuangMusicPath(path)) {
        ShowSongs(song_page_.offset);
        Message("Library changed or SD unavailable. Select again.");
        return;
    }
    song_status_ = -1;
    ESP_LOGI("LeleSongs", "Indexed playback requested");
    Message(("Starting: " + path.substr(8)).c_str());
}

void LeleMenu::PollSongs() {
    if (!songs_page_ || !IsOpen())
        return;
    const auto now = uint32_t(esp_timer_get_time() / 1000);
    if (uint32_t(now - song_polled_at_) < 1000)
        return;
    song_polled_at_ = now;
    auto page = GetLichuangMusicPage(song_page_.offset);
    if (page.revision != song_page_.revision || page.ready != song_page_.ready ||
        page.scanning != song_page_.scanning || page.blocked != song_page_.blocked ||
        page.error != song_page_.error) {
        ShowSongs(page.offset);
    } else if (Application::GetInstance().GetMusicStatus() != song_status_) {
        UpdateSongStatus();
    }
}

void LeleMenu::ChangeTheme() {
    if (!display_settings_ || preparing_ || transfer_boot_)
        return;
    auto* theme = LvglThemeManager::GetInstance().GetTheme(IsDark() ? "light" : "dark");
    if (display_ && theme) {
        display_->SetTheme(theme);  // Existing display API also persists the choice.
        ShowDisplaySettings();
    }
}

void LeleMenu::ChangeBrightness(int delta) {
    if (!display_settings_ || preparing_ || transfer_boot_ || !backlight_)
        return;
    const int target = std::clamp(brightness_ + delta, 5, 100);
    if (target == brightness_)
        return;
    brightness_ = target;
    backlight_->SetBrightness(brightness_);
    // Save the requested target, rather than a sample of the PWM fade. Avoid
    // relying on SetBrightness's early return when actual brightness is equal.
    Settings settings("display", true);
    settings.SetInt("brightness", brightness_);
    char text[40];
    snprintf(text, sizeof(text), "Brightness: %d%%", brightness_);
    Message(text);
}

void LeleMenu::ChangeOrientation() {
#ifdef CONFIG_USE_DEFAULT_MESSAGE_STYLE
    if (!orientation_display_ || !display_settings_ || !IsOpen() || preparing_ || transfer_boot_ ||
        restart_to_transfer_)
        return;
    const bool portrait = !orientation_display_->IsPortrait();
    if (!orientation_display_->SetPortrait(portrait)) {
        Message("Display is busy. Try again.");
        return;
    }
    {
        Settings settings("display", true);
        settings.SetInt("portrait", portrait ? 1 : 0);
    }
    ESP_LOGI("LeleDisplay", "Orientation: %s (%dx%d)", portrait ? "portrait" : "landscape",
             display_->width(), display_->height());
    ShowDisplaySettings();
#endif
}

void LeleMenu::Message(const char* text) {
    if (lvgl_port_lock(1000)) {
        if (message_)
            lv_label_set_text(message_, text);
        lvgl_port_unlock();
    }
}

void LeleMenu::Close() {
    if (wifi_transfer_) {
        wifi_closing_ = true;
        Message("Finishing transfer...\nPlease wait.");
        return;
    }
    if (preparing_ || transfer_boot_ || restart_to_transfer_)
        return;
    if (lvgl_port_lock(1000)) {
        assistant_tap_.Cancel();
        assistant_tap_pending_ = false;
        pending_song_.clear();
        if (overlay_)
            lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
        open_ = false;
        lvgl_port_unlock();
    }
}

void LeleMenu::Toggle() {
    if (transfer_boot_)
        return;
    // All mutations other than atomic input run on the main loop.
    Application::GetInstance().Schedule([this]() {
#if defined(CONFIG_USE_DEFAULT_MESSAGE_STYLE)
        if (orientation_display_)
            orientation_display_->SetPowerSaveMode(false);
#endif
        if (IsOpen())
            Close();
        else {
            display_settings_ = false;
            songs_page_ = false;
            Show("Manage SD card files on your Mac.");
        }
    });
}

bool LeleMenu::HandleClick() {
#if defined(CONFIG_USE_DEFAULT_MESSAGE_STYLE)
    if (orientation_display_ && orientation_display_->IsScreenOff()) {
        screen_wake_pending_ = true;
        return true;
    }
#endif
    if (!IsOpen() && !transfer_boot_)
        return false;
    action_ = transfer_boot_ ? Action::Return : Action::Close;
    return true;
}

void LeleMenu::Enter(bool network) {
    if (wifi_transfer_ || transfer_boot_)
        return;
    if (restart_to_transfer_)
        esp_restart();
    if (preparing_)
        return;
    auto& app = Application::GetInstance();
    const auto state = app.GetDeviceState();
    if (state != kDeviceStateIdle && state != kDeviceStateNotifying) {
        Message("Finish the conversation, then try again.");
        return;
    }
    if (network && !WifiManager::GetInstance().IsConnected()) {
        Message("Connect Wi-Fi first.");
        return;
    }
    wifi_transfer_ = network;
    if (network)
        display_settings_ = songs_page_ = false;
    wifi_started_ = false;
    wifi_closing_ = false;
    wifi_exit_message_ = "Transfer ended.";
    preparing_ = true;
    prepare_started_ = esp_timer_get_time();
    BeginLichuangStorageTransfer();
    app.SetMusicStorageBlocked(true);
    if (network)
        Show("Stopping music and saving index...\nPlease wait.");
    else
        Message("Stopping music and saving index...\nPlease wait.");
}

void LeleMenu::PollWifiUpload() {
    auto& app = Application::GetInstance();
    if (wifi_closing_) {
        wifi_upload_.RequestStop();
        if (!wifi_upload_.Stopped())
            return;
        // HTTP task has returned and closed its files before scans/playback resume.
        bool refresh = true;
        if (wifi_started_)
            refresh = FinishLichuangStorageUpload();
        else
            CancelLichuangStorageTransfer();
        app.SetMusicStorageBlocked(false);
        preparing_ = wifi_transfer_ = wifi_started_ = wifi_closing_ = false;
        Show(refresh ? wifi_exit_message_ : "Index refresh failed; retry.");
        return;
    }
    if (preparing_) {
        if (esp_timer_get_time() - prepare_started_ > 30000000) {
            wifi_exit_message_ = "SD busy; try again.";
            wifi_closing_ = true;
            Message("SD is still busy.\nClosing transfer...");
            return;
        }
        if (app.IsMusicWorkerBusy() || !IsLichuangStorageQuiet())
            return;
        const char* failure = nullptr;
        if (!IsLichuangStorageReadyForUpload())
            failure = "SD not ready.";
        else if (!MarkLichuangStorageDirty())
            failure = "Cannot save SD state.";
        else if (!wifi_upload_.Start())
            failure = "Cannot start Wi-Fi server.";
        if (failure) {
            wifi_exit_message_ = failure;
            wifi_closing_ = true;
            Message(failure);
            return;
        }
        preparing_ = false;
        wifi_started_ = true;
        wifi_saved_ = unsigned(-1);
    }
    if (wifi_upload_.NeedsStop() || !WifiManager::GetInstance().IsConnected()) {
        wifi_closing_ = true;
        Message("Session ended.\nClosing transfer...");
        return;
    }
    if (wifi_saved_ != wifi_upload_.Saved()) {
        wifi_saved_ = wifi_upload_.Saved();
        char text[200];
        std::snprintf(text, sizeof(text),
                      "%s\nCode: %s\nChanges: %u\n64 MiB/file; 10-minute session\n"
                      "Manage SD files in browser.\nEnd transfer to update Songs.",
                      wifi_upload_.Address().c_str(), wifi_upload_.Code(), wifi_saved_);
        Message(text);
    }
}

void LeleMenu::Return() {
    if (LeleUsb::Finish() != ESP_OK) {
        Message(LeleUsb::Failed() ? "SD error. Press RESET to\nreturn to the assistant."
                                  : "Eject disk, then tap Return.\nOr restart the device.");
        return;
    }
    usb_ready_ = false;
    if (LeleUsb::SaveBootMode(LeleUsb::BootMode::Refresh) != ESP_OK) {
        Message("Could not save mode.\nPress Return to retry.");
        return;
    }
    esp_restart();
}

void LeleMenu::PollScreenIdle() {
#if defined(CONFIG_USE_DEFAULT_MESSAGE_STYLE)
    if (!orientation_display_)
        return;
    if (screen_wake_pending_.exchange(false))
        orientation_display_->SetPowerSaveMode(false);
    const auto state = Application::GetInstance().GetDeviceState();
    orientation_display_->PollScreenIdle(IsOpen() || transfer_boot_ || preparing_ ||
                                         restart_to_transfer_ || wifi_transfer_ ||
                                         state != kDeviceStateIdle);
#endif
}

void LeleMenu::Poll() {
    PollScreenIdle();
    // Board construction precedes Application::Initialize/SetupUI. Installing
    // the region in Start would leave it below the later full-screen widgets.
    // Poll runs on the application task after initialization has completed.
    if (!transfer_boot_ && !assistant_region_ && display_ && display_->IsSetupUICalled())
        InitializeAssistantTouch();
    switch (action_.exchange(Action::None)) {
        case Action::Songs:
            ShowSongs(0);
            break;
        case Action::PreviousSongs:
            if (songs_page_ && song_page_.offset >= TfMusic::kBrowserPageSize)
                ShowSongs(song_page_.offset - TfMusic::kBrowserPageSize);
            break;
        case Action::NextSongs:
            if (songs_page_ && song_page_.offset + TfMusic::kBrowserPageSize < song_page_.total)
                ShowSongs(song_page_.offset + TfMusic::kBrowserPageSize);
            break;
        case Action::PlaySong:
            PlaySong(false);
            break;
        case Action::SongDetails:
            PlaySong(true);
            break;
        case Action::StopSong:
            if (songs_page_ && !preparing_ && !transfer_boot_) {
                Application::GetInstance().RequestMusicStop();
                song_status_ = -1;
                Message("Stopping...");
            }
            break;
        case Action::DisplaySettings:
            ShowDisplaySettings();
            break;
        case Action::Theme:
            ChangeTheme();
            break;
        case Action::Dim:
            ChangeBrightness(-10);
            break;
        case Action::Brighten:
            ChangeBrightness(10);
            break;
        case Action::Orientation:
            ChangeOrientation();
            break;
        case Action::MainMenu:
            if (!preparing_ && !transfer_boot_ && !restart_to_transfer_ && !wifi_transfer_) {
                display_settings_ = false;
                songs_page_ = false;
                Show("Manage SD card files on your Mac.");
            }
            break;
        case Action::Enter:
            Enter();
            break;
        case Action::WifiEnter:
            Enter(true);
            break;
        case Action::Close:
            Close();
            break;
        case Action::Return:
            if (transfer_boot_)
                Return();
            break;
        default:
            break;
    }
    PollSongs();
    if (assistant_tap_pending_.exchange(false) && !IsOpen() && !transfer_boot_ && !preparing_ &&
        !restart_to_transfer_) {
        auto& app = Application::GetInstance();
        const auto state = app.GetDeviceState();
        if (state == kDeviceStateIdle || state == kDeviceStateListening ||
            state == kDeviceStateSpeaking || state == kDeviceStateNotifying) {
            ESP_LOGI("LeleTouch", "Assistant tap in state %d", state);
            app.ToggleChatState();
        }
    }
    if (wifi_transfer_) {
        PollWifiUpload();
        return;
    }
    if (!preparing_)
        return;
    auto& app = Application::GetInstance();
    if (!app.IsMusicWorkerBusy()) {
        // Persist before unmount. A failed NVS write must leave the live card mounted.
        // Wait for the index worker (including fclose/readback) before committing.
        if (IsLichuangStorageQuiet()) {
            const auto saved = LeleUsb::SaveBootMode(LeleUsb::BootMode::Transfer);
            if (saved == ESP_OK) {
                if (UnmountLichuangMusicCard()) {
                    ESP_LOGI("LeleMenu", "SD closed; restarting into USB transfer");
                    esp_restart();
                }
                // A partial unmount cannot safely be undone in-place. Keep
                // playback/indexing gated and the persistent transfer mode.
                restart_to_transfer_ = true;
                preparing_ = false;
                Message("Card close failed. Select USB\nFile Transfer to restart safely.");
                return;
            }
            LeleUsb::SaveBootMode(LeleUsb::BootMode::Assistant);
            CancelLichuangStorageTransfer();
            app.SetMusicStorageBlocked(false);
            preparing_ = false;
            Message("Cannot enter transfer mode.\nCard and assistant kept available.");
            return;
        }
    }
    if (esp_timer_get_time() - prepare_started_ > 30000000) {
        CancelLichuangStorageTransfer();
        app.SetMusicStorageBlocked(false);
        preparing_ = false;
        Message("Card is still busy.\nPlease try again later.");
    }
}

void LeleMenu::HandleAssistantTouch(lv_event_t* event) {
    // Called under the LVGL lock: never access Application or audio here.
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED &&
        code != LV_EVENT_PRESS_LOST)
        return;
#if defined(CONFIG_USE_DEFAULT_MESSAGE_STYLE)
    if (code == LV_EVENT_PRESSED) {
        screen_wake_gesture_ = orientation_display_ && orientation_display_->IsScreenOff();
        screen_wake_pending_ = true;
    } else if (code == LV_EVENT_PRESSING) {
        screen_wake_pending_ = true;
    }
    if (screen_wake_gesture_) {
        assistant_tap_.Cancel();
        assistant_tap_pending_ = false;
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
            screen_wake_gesture_ = false;
        return;
    }
#endif
    auto* input = lv_indev_active();
    if (!input) {
        assistant_tap_.Cancel();
        return;
    }
    lv_point_t point;
    lv_indev_get_point(input, &point);
    const bool enabled = !IsOpen() && !transfer_boot_;
    switch (code) {
        case LV_EVENT_PRESSED:
            ESP_LOGI("LeleTouch", "Press x=%ld y=%ld enabled=%d", long(point.x), long(point.y),
                     enabled);
            assistant_tap_.Begin(point.x, point.y, lv_tick_get(), enabled);
            break;
        case LV_EVENT_PRESSING:
            assistant_tap_.Move(point.x, point.y);
            break;
        case LV_EVENT_RELEASED: {
            const bool accepted = assistant_tap_.End(point.x, point.y, lv_tick_get(), enabled);
            ESP_LOGI("LeleTouch", "Release x=%ld y=%ld accepted=%d", long(point.x), long(point.y),
                     accepted);
            if (accepted)
                assistant_tap_pending_ = true;
            break;
        }
        case LV_EVENT_PRESS_LOST:
            assistant_tap_.Cancel();
            break;
        default:
            break;
    }
}

void LeleMenu::InitializeAssistantTouch() {
    if (!lvgl_port_lock(1000)) {
        ESP_LOGW("LeleTouch", "Cannot install assistant touch region");
        return;
    }
    // Cover the normal assistant screen, below top-layer menus, without
    // changing the display's bitmap or font rendering.
    auto* region = EnsureAssistantTouchRegion(
        display_ && display_->IsSetupUICalled(), transfer_boot_, assistant_region_,
        [](lv_event_t* event) {
            static_cast<LeleMenu*>(lv_event_get_user_data(event))->HandleAssistantTouch(event);
        },
        this);
    if (!region) {
        lvgl_port_unlock();
        return;
    }
    assistant_region_ = region;
    lv_obj_update_layout(region);
    lv_area_t bounds;
    lv_obj_get_coords(region, &bounds);
    ESP_LOGI("LeleTouch", "Region ready after SetupUI (%ld,%ld)-(%ld,%ld)", long(bounds.x1),
             long(bounds.y1), long(bounds.x2), long(bounds.y2));
    lvgl_port_unlock();
}

void LeleMenu::Start() {
    esp_timer_create_args_t config = {};
    config.callback = [](void* arg) {
        auto* menu = static_cast<LeleMenu*>(arg);
        if (!menu->poll_queued_.exchange(true)) {
            Application::GetInstance().Schedule([menu]() {
                menu->Poll();
                menu->poll_queued_ = false;
            });
        }
    };
    config.arg = this;
    config.name = "lele_menu";
    ESP_ERROR_CHECK(esp_timer_create(&config, &timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, 100000));
}

void LeleMenu::RunTransferBootIfRequested() {
    if (!LeleUsb::TakeTransferBootRequest())
        return;
    transfer_boot_ = true;
    Show("Preparing SD card...", true);
    usb_ready_ = LeleUsb::Start() == ESP_OK;
    Message(usb_ready_ ? "Open SD disk on computer.\nEject before tapping Return."
                       : "SD/USB setup failed.\nReturn to retry in assistant mode.");
    bool previous_released = false;
    // Deliberately never construct camera/music tools or start Application audio.
    while (true) {
        const bool released = usb_ready_ && LeleUsb::HostReleased();
        if (released != previous_released) {
            Message(released ? "SD disk ejected.\nYou can return to the assistant."
                             : "Open SD disk on computer.\nEject before tapping Return.");
            previous_released = released;
        }
        Poll();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif
