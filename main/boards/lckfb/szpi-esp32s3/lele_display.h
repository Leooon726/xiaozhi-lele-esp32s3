#pragma once
#include "sdkconfig.h"
#if defined(CONFIG_LICHUANG_USB_STORAGE) && defined(CONFIG_USE_DEFAULT_MESSAGE_STYLE)
#include <esp_lvgl_port.h>
#include "display/lcd_display.h"
#include "lele_display_layout.h"

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
    }

    bool IsPortrait() const { return portrait_; }

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

    void AdaptLayout() {
        LeleAssistantLayout{
            container_,          top_bar_,      status_bar_,         bottom_bar_,
            chat_message_label_, status_label_, notification_label_, low_battery_popup_}
            .MakeResponsive();
    }
};
#endif
