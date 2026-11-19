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

inline void ConfigureSongListButton(lv_obj_t* button, const lv_font_t* font) {
    lv_obj_set_height(button, 30);
    auto* label = lv_obj_get_child(button, 0);
    lv_obj_set_size(label, lv_pct(96), 28);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    if (font)
        lv_obj_set_style_text_font(label, font, 0);
    lv_obj_center(label);
}
