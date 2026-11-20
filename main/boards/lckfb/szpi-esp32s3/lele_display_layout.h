#pragma once
#include <lvgl.h>
#include <initializer_list>

// The default assistant uses pixel widths at creation. Make only this board's
// widgets follow the screen; percentage widths also preserve existing text.
struct LeleAssistantLayout {
    lv_obj_t* background;
    lv_obj_t* top_bar;
    lv_obj_t* status_bar;
    lv_obj_t* bottom_bar;
    lv_obj_t* subtitle;
    lv_obj_t* status;
    lv_obj_t* notification;
    lv_obj_t* low_battery;

    void MakeResponsive() const {
        lv_obj_set_size(background, lv_pct(100), lv_pct(100));
        for (auto* bar : {top_bar, status_bar, bottom_bar})
            lv_obj_set_width(bar, lv_pct(100));
        lv_obj_set_width(subtitle, lv_pct(100));
        lv_obj_set_width(status, lv_pct(75));
        lv_obj_set_width(notification, lv_pct(75));
        lv_obj_set_width(low_battery, lv_pct(90));
    }
};
