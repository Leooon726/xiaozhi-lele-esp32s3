#pragma once
#include <lvgl.h>

inline lv_obj_t* CreateLeleButton(lv_obj_t* parent, const char* text, bool dark) {
    auto* button = lv_button_create(parent);
    lv_obj_set_size(button, lv_pct(100), 32);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(dark ? 0x303030 : 0xDDDDDD), 0);
    lv_obj_set_style_text_color(button, dark ? lv_color_white() : lv_color_black(), 0);
    auto* caption = lv_label_create(button);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    return button;
}

inline lv_obj_t* CreateSongListRows(lv_obj_t* overlay) {
    auto* rows = lv_obj_create(overlay);
    lv_obj_remove_style_all(rows);
    lv_obj_set_width(rows, lv_pct(100));
    lv_obj_set_height(rows, 0);
    lv_obj_set_flex_grow(rows, 1);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rows, 2, 0);
    lv_obj_remove_flag(rows, LV_OBJ_FLAG_SCROLLABLE);
    return rows;
}

inline lv_obj_t* CreateSongListFooter(lv_obj_t* overlay) {
    auto* footer = lv_obj_create(overlay);
    lv_obj_remove_style_all(footer);
    lv_obj_set_size(footer, lv_pct(100), 30);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(footer, 4, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    return footer;
}

inline void RefreshSongNameScroll(lv_obj_t* viewport) {
    auto* label = lv_obj_get_child(viewport, 0);
    lv_anim_delete(label, nullptr);
    lv_obj_set_x(label, 0);
    const int32_t overflow = lv_obj_get_width(label) - lv_obj_get_content_width(viewport);
    if (overflow <= 0)
        return;

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, label);
    lv_anim_set_values(&animation, 0, -overflow);
    lv_anim_set_duration(&animation, lv_anim_speed_to_time(40, 0, -overflow));
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    // Leave the last character visible, then jump to the beginning. No reverse
    // leg and no second copy of the text following the first one.
    lv_anim_set_repeat_delay(&animation, 1000);
    lv_anim_set_exec_cb(&animation, [](void* object, int32_t x) {
        lv_obj_set_x(static_cast<lv_obj_t*>(object), x);
    });
    lv_anim_start(&animation);
}

inline void ConfigureSongListButton(lv_obj_t* button, const lv_font_t* font) {
    lv_obj_set_height(button, 30);
    auto* label = lv_obj_get_child(button, 0);
    if (font)
        lv_obj_set_style_text_font(label, font, 0);
    auto* viewport = lv_obj_create(button);
    lv_obj_remove_style_all(viewport);
    lv_obj_set_width(viewport, lv_pct(96));
    lv_obj_set_height(viewport,
                      lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
    lv_obj_remove_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(viewport, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(viewport);
    lv_obj_set_parent(label, viewport);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    // Recalculate after the text or its available width changes. The moving
    // label is clipped by this fixed viewport and never takes pointer events.
    auto refresh = [](lv_event_t* event) {
        RefreshSongNameScroll(static_cast<lv_obj_t*>(lv_event_get_user_data(event)));
    };
    lv_obj_add_event_cb(viewport, refresh, LV_EVENT_SIZE_CHANGED, viewport);
    lv_obj_add_event_cb(label, refresh, LV_EVENT_SIZE_CHANGED, viewport);
}
