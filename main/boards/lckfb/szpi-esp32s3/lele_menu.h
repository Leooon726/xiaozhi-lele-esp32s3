#pragma once
#include "sdkconfig.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <esp_timer.h>
#include <lvgl.h>
#include <atomic>
#include "assistant_tap.h"

class Display;
class Backlight;

class LeleMenu {
public:
    void Configure(Display* display, Backlight* backlight);
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
        Close,
        Return,
        DisplaySettings,
        Theme,
        Dim,
        Brighten,
        MainMenu
    };
    struct Binding {
        LeleMenu* menu;
        Action action;
    };
    Binding bindings_[4]{};
    std::atomic<Action> action_{Action::None};
    std::atomic<bool> open_{false};
    std::atomic<bool> poll_queued_{false};
    std::atomic<bool> transfer_boot_{false};
    bool preparing_ = false;
    bool restart_to_transfer_ = false;
    bool usb_ready_ = false;
    int64_t prepare_started_ = 0;
    esp_timer_handle_t timer_ = nullptr;
    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* message_ = nullptr;
    AssistantTap assistant_tap_;
    std::atomic<bool> assistant_tap_pending_{false};
    Display* display_ = nullptr;
    Backlight* backlight_ = nullptr;
    int brightness_ = 75;
    bool display_settings_ = false;
    void Show(const char* text, bool transfer = false);
    void ShowDisplaySettings();
    void ChangeTheme();
    void ChangeBrightness(int delta);
    bool IsDark() const;
    void Message(const char* text);
    void Close();
    void Poll();
    void Enter();
    void Return();
    void AddButton(const char* label, Action action, unsigned slot);
    void InitializeAssistantTouch();
    void HandleAssistantTouch(lv_event_t* event);
};
#endif
