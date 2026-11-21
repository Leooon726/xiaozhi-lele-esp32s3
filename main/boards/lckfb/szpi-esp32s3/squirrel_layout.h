#pragma once
#include <lvgl.h>

inline void ConfigureSquirrelLayout(lv_obj_t* box, lv_obj_t* image) {
    // Overflow alone still inherits the parent's old draw bounds in LVGL.
    // Give the transparent parent the whole viewport, preserving its center.
    if (box) {
        lv_obj_set_size(box, lv_pct(100), lv_pct(100));
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_image_set_scale(image, 461);  // 1.8x in LVGL's 256-based units.
}
