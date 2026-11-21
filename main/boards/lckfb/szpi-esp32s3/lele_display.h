#pragma once
#include "sdkconfig.h"
#if defined(CONFIG_LICHUANG_USB_STORAGE) && defined(CONFIG_USE_DEFAULT_MESSAGE_STYLE)
#include <esp_lcd_panel_ops.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <algorithm>
#include <cstring>
#include "assets/lang_config.h"
#include "backlight.h"
#include "display/lcd_display.h"
#include "lele_display_layout.h"
#include "screen_idle.h"
#include "settings.h"
#include "squirrel_images.h"

// Board-local rotation: no change to other SPI displays or alternate UI styles.
class LeleLcdDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    void SetupUI() override {
        if (IsSetupUICalled())
            return;
        SpiLcdDisplay::SetupUI();
        if (!lvgl_port_lock(1000))
            return;
        AdaptLayout();
        lvgl_port_unlock();
        SetEmotion("neutral");
    }

    bool IsPortrait() const { return portrait_; }

    void ConfigureScreenIdle(Backlight* backlight) { backlight_ = backlight; }
    bool IsScreenOff() const { return screen_off_.load(); }

    // All panel/backlight work stays on the application task. Touch callbacks
    // only queue a request through LeleMenu; audio wake uses the Display API.
    void PollScreenIdle(bool keep_awake) {
        if (!IsSetupUICalled() || !backlight_)
            return;
        if (wake_requested_ || keep_awake) {
            SetPowerSaveMode(false);
            return;
        }
        if (screen_idle_.Due(esp_timer_get_time(), false) && !screen_off_)
            SetPowerSaveMode(true);
    }

    void SetPowerSaveMode(bool on) override {
        const auto now = esp_timer_get_time();
        if (!on) {
            screen_idle_.Activity(now);
            wake_requested_ = true;
        }
        if (!IsSetupUICalled() || !backlight_)
            return;
        if (!on && !screen_off_) {
            wake_requested_ = false;
            return;
        }
        if (on && (screen_off_ || wake_requested_))
            return;
        if (!lvgl_port_lock(1000))
            return;  // Keep the wake request pending, or retry sleep on the next poll.
        // Check the input clock under the same lock as panel-off. A press that
        // arrived just after the menu sampled its queue must prevent sleep.
        if (on && lv_display_get_inactive_time(display_) < LeleScreenIdle::kTimeoutUs / 1000) {
            lvgl_port_unlock();
            return;
        }
        const auto result = esp_lcd_panel_disp_on_off(panel_, !on);
        if (result == ESP_OK) {
            if (on) {
                Settings settings("display");
                const int saved = settings.GetInt("brightness", 75);
                wake_brightness_ = saved <= 0 ? 10 : std::min(saved, 100);
                backlight_->SetBrightness(0, false);
                screen_off_ = true;
            } else {
                backlight_->SetBrightness(wake_brightness_, false);
                screen_off_ = false;
                wake_requested_ = false;
                lv_obj_invalidate(lv_display_get_screen_active(display_));
            }
            ESP_LOGI("LeleScreen", "Display %s", on ? "off" : "on");
        } else {
            ESP_LOGW("LeleScreen", "Display power request failed: %d", result);
            if (on)
                screen_idle_.Activity(now);  // Avoid repeating a sleep error every 100ms.
        }
        lvgl_port_unlock();
    }

    void SetStatus(const char* status) override {
        if (!status)
            return;
        SpiLcdDisplay::SetStatus(status);
        activity_ =
            std::strcmp(status, Lang::Strings::LISTENING) == 0    ? SquirrelExpression::Listening
            : std::strcmp(status, Lang::Strings::SPEAKING) == 0   ? SquirrelExpression::Speaking
            : std::strcmp(status, Lang::Strings::CONNECTING) == 0 ? SquirrelExpression::Thinking
                                                                  : SquirrelExpression::Neutral;
        ShowSquirrel(activity_.load());
    }

    bool SetEmotion(const char* emotion) override {
        if (!emotion || !*emotion)
            return false;
        auto expression = SquirrelEmotion(emotion);
        if (expression == SquirrelExpression::Unsupported)
            return false;
        if (expression == SquirrelExpression::Neutral)
            expression = activity_.load();
        return ShowSquirrel(expression);
    }

    // Called only on the main task (or during board construction), never in a
    // touch callback. LVGL rotates input points; keep FT5x06's base mapping.
    bool SetPortrait(bool portrait) {
        if (!display_ || !lvgl_port_lock(1000))
            return false;
        for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
            if (lv_indev_get_display(input) == display_) {
                lv_indev_reset(input, nullptr);
                lv_indev_wait_release(input);
            }
        }
        lv_display_set_rotation(display_,
                                portrait ? LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_0);
        width_ = lv_display_get_horizontal_resolution(display_);
        height_ = lv_display_get_vertical_resolution(display_);
        portrait_ = portrait;
        if (IsSetupUICalled())
            AdaptLayout();
        if (preview_image_)
            lv_obj_set_size(preview_image_, width_ / 2, height_ / 2);
        if (preview_image_cached_) {
            const auto* image = preview_image_cached_->image_dsc();
            if (image->header.w)
                lv_image_set_scale(preview_image_, 128 * width_ / image->header.w);
        }
        lv_obj_update_layout(lv_display_get_screen_active(display_));
        lv_obj_invalidate(lv_display_get_screen_active(display_));
        lvgl_port_unlock();
        return true;
    }

private:
    bool portrait_ = false;
    std::atomic<SquirrelExpression> activity_{SquirrelExpression::Neutral};
    Backlight* backlight_ = nullptr;
    LeleScreenIdle screen_idle_;
    std::atomic<bool> screen_off_{false};
    bool wake_requested_ = false;
    int wake_brightness_ = 75;

    bool ShowSquirrel(SquirrelExpression expression) {
        if (!IsSetupUICalled() || !emoji_image_ || !lvgl_port_lock(1000))
            return false;
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }
        const auto* image = GetSquirrelImage(expression);
        if (lv_image_get_src(emoji_image_) != image)
            lv_image_set_src(emoji_image_, image);
        // LVGL uses 256 for 1x; keep the original center and all seven sources.
        lv_image_set_scale(emoji_image_, 461);  // 1.8x, rounded to LVGL precision.
        if (emoji_box_)
            lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        emotion_uses_image_ = true;
        ShowEmotion();
        lvgl_port_unlock();
        return true;
    }

    void AdaptLayout() {
        LeleAssistantLayout{
            container_,          top_bar_,      status_bar_,         bottom_bar_,
            chat_message_label_, status_label_, notification_label_, low_battery_popup_}
            .MakeResponsive();
    }
};
#endif
