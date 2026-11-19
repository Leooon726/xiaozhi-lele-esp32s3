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
#include "backlight.h"
#include "display.h"
#include "lvgl_theme.h"
#include "music_storage.h"
#include "settings.h"
#include "usb_transfer.h"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

void LeleMenu::Configure(Display* display, Backlight* backlight) {
    display_ = display;
    backlight_ = backlight;
}

bool LeleMenu::IsDark() const {
    auto* theme = display_ ? display_->GetTheme() : nullptr;
    return theme && theme->name() == "dark";
}

void LeleMenu::AddButton(const char* label, Action action, unsigned slot) {
    auto* button = lv_button_create(overlay_);
    lv_obj_set_size(button, lv_pct(100), 32);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(IsDark() ? 0x303030 : 0xDDDDDD), 0);
    lv_obj_set_style_text_color(button, IsDark() ? lv_color_white() : lv_color_black(), 0);
    auto* caption = lv_label_create(button);
    lv_label_set_text(caption, label);
    lv_obj_center(caption);
    bindings_[slot] = {this, action};
    lv_obj_add_event_cb(
        button,
        [](lv_event_t* event) {
            auto* binding = static_cast<Binding*>(lv_event_get_user_data(event));
            binding->menu->action_.store(binding->action);
        },
        LV_EVENT_CLICKED, &bindings_[slot]);
}

void LeleMenu::Show(const char* text, bool transfer) {
    if (!lvgl_port_lock(1000))
        return;
    // Rebuild only on page navigation, never from a touch callback. Input
    // callbacks merely queue an action, so deleted controls cannot be reused.
    if (overlay_)
        lv_obj_delete(overlay_);
    {
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
        auto* title = lv_label_create(overlay_);
        lv_label_set_text(
            title, transfer ? "USB File Transfer" : (display_settings_ ? "Display" : "Lele Menu"));
        message_ = lv_label_create(overlay_);
        lv_obj_set_width(message_, lv_pct(100));
        lv_obj_set_height(message_, display_settings_ ? 28 : 48);
        lv_label_set_long_mode(message_, LV_LABEL_LONG_WRAP);
        if (transfer) {
            AddButton("Return to Assistant", Action::Return, 0);
        } else if (display_settings_) {
            AddButton(IsDark() ? "Theme: Dark" : "Theme: Light", Action::Theme, 0);
            AddButton("Brightness -", Action::Dim, 1);
            AddButton("Brightness +", Action::Brighten, 2);
            AddButton("Back", Action::MainMenu, 3);
        } else {
            AddButton("USB File Transfer", Action::Enter, 0);
            AddButton("Display", Action::DisplaySettings, 1);
            AddButton("Back", Action::Close, 2);
        }
    }
    lv_label_set_text(message_, text);
    lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    open_ = true;
    lvgl_port_unlock();
}

void LeleMenu::ShowDisplaySettings() {
    if (preparing_ || transfer_boot_ || restart_to_transfer_)
        return;
    display_settings_ = true;
    Settings settings("display", false);
    brightness_ = std::clamp<int32_t>(settings.GetInt("brightness", 75), 5, 100);
    char text[40];
    snprintf(text, sizeof(text), "Brightness: %d%%", brightness_);
    Show(text);
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

void LeleMenu::Message(const char* text) {
    if (lvgl_port_lock(1000)) {
        if (message_)
            lv_label_set_text(message_, text);
        lvgl_port_unlock();
    }
}

void LeleMenu::Close() {
    if (preparing_ || transfer_boot_ || restart_to_transfer_)
        return;
    if (lvgl_port_lock(1000)) {
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
        if (IsOpen())
            Close();
        else {
            display_settings_ = false;
            Show("Manage SD card files on your Mac.");
        }
    });
}

bool LeleMenu::HandleClick() {
    if (!IsOpen() && !transfer_boot_)
        return false;
    action_ = transfer_boot_ ? Action::Return : Action::Close;
    return true;
}

void LeleMenu::Enter() {
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
    preparing_ = true;
    prepare_started_ = esp_timer_get_time();
    BeginLichuangStorageTransfer();
    app.SetMusicStorageBlocked(true);
    Message("Stopping music and saving index...\nPlease wait.");
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

void LeleMenu::Poll() {
    switch (action_.exchange(Action::None)) {
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
        case Action::MainMenu:
            if (!preparing_ && !transfer_boot_ && !restart_to_transfer_) {
                display_settings_ = false;
                Show("Manage SD card files on your Mac.");
            }
            break;
        case Action::Enter:
            Enter();
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
