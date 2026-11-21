#pragma once
#include "sdkconfig.h"
#if defined(CONFIG_LICHUANG_USB_STORAGE) && defined(CONFIG_USE_DEFAULT_MESSAGE_STYLE)
#include <esp_lvgl_port.h>
#include <cstring>
#include "assets/lang_config.h"
#include "display/lcd_display.h"
#include "lele_display_layout.h"
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
