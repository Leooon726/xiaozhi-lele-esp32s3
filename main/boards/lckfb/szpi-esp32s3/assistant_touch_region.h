#pragma once
#include <lvgl.h>

// SetupUI creates full-screen siblings. A region created before it is hidden
// behind them, even though its own gesture callback is correctly installed.
inline lv_obj_t* EnsureAssistantTouchRegion(bool ui_ready, bool transfer_boot, lv_obj_t* existing,
                                            lv_event_cb_t callback, void* context) {
    if (existing || !ui_ready || transfer_boot)
        return existing;
    auto* region = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(region);
    lv_obj_set_size(region, lv_pct(60), lv_pct(50));
    lv_obj_center(region);
    lv_obj_set_style_bg_opa(region, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(region, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(region, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(region, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(region, callback, LV_EVENT_ALL, context);
    return region;
}
