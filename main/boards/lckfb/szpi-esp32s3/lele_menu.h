#pragma once
#include "sdkconfig.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <esp_timer.h>
#include <lvgl.h>
#include <atomic>

class LeleMenu {
public:
    void RunTransferBootIfRequested();
    void Start();
    void Toggle();
    bool HandleClick();
    bool IsOpen() const { return open_.load(); }
    bool IsTransferBoot() const { return transfer_boot_; }

private:
    enum class Action { None, Enter, Close, Return };
    struct Binding {
        LeleMenu* menu;
        Action action;
    };
    Binding bindings_[2]{};
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
    void Show(const char* text, bool transfer = false);
    void Message(const char* text);
    void Close();
    void Poll();
    void Enter();
    void Return();
    void AddButton(const char* label, Action action, unsigned slot);
};
#endif
