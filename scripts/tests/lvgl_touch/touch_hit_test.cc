#include <cassert>
#include <string>
#include "assistant_touch_region.h"
#include "lele_display_layout.h"
#include "song_list_layout.h"

static void Event(lv_event_t*) {}

static void CheckSongScrolling(int width) {
    auto tall_font = *LV_FONT_DEFAULT;
    tall_font.line_height = 32;  // Reproduce fonts taller than the old 28px label.
    const lv_font_t* fonts[] = {LV_FONT_DEFAULT, &tall_font};
    for (const auto* font : fonts) {
        auto* button = CreateLeleButton(lv_layer_top(), "Short", true);
        lv_obj_set_width(button, width - 12);
        ConfigureSongListButton(button, font);
        auto* label = lv_obj_get_child(button, 0);
        lv_obj_update_layout(button);
        assert(lv_obj_get_content_height(label) == lv_font_get_line_height(font));
        assert(lv_anim_get(label, nullptr) == nullptr);
        lv_label_set_text(
            label, "A very long song name that cannot fit in either screen orientation at all");
        lv_obj_update_layout(button);
        auto* animation = lv_anim_get(label, nullptr);
        assert(animation && -animation->end_value > lv_obj_get_content_width(label));
        assert(lv_obj_get_content_height(label) == lv_font_get_line_height(font));
        lv_label_set_text(label, "Short");
        lv_obj_update_layout(button);
        assert(lv_anim_get(label, nullptr) == nullptr);
        lv_obj_delete(button);
    }
}

static void CheckOrientation(lv_display_t* display, lv_display_rotation_t rotation,
                             lv_obj_t* region) {
    lv_display_set_rotation(display, rotation);
    const int width = rotation == LV_DISPLAY_ROTATION_0 ? 320 : 240;
    const int height = rotation == LV_DISPLAY_ROTATION_0 ? 240 : 320;
    assert(lv_display_get_horizontal_resolution(display) == width);
    assert(lv_display_get_vertical_resolution(display) == height);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t bounds;
    lv_obj_get_coords(region, &bounds);
    assert(bounds.x1 == 0 && bounds.y1 == 0 && bounds.x2 == width - 1 && bounds.y2 == height - 1);
    lv_point_t raw[] = {{0, 0}, {319, 0}, {0, 239}, {319, 239}, {160, 120}};
    for (const auto& base : raw) {
        auto point = base;
        lv_display_rotate_point(display, &point);
        assert(point.x == (rotation == LV_DISPLAY_ROTATION_0 ? base.x : 239 - base.y));
        assert(point.y == (rotation == LV_DISPLAY_ROTATION_0 ? base.y : base.x));
        assert(lv_indev_search_obj(lv_screen_active(), &point) == region);
    }

    // Exercise the same responsive widget policy used by LeleLcdDisplay, with
    // existing subtitle text retained across both screen sizes.
    auto* screen = lv_screen_active();
    auto* background = lv_obj_create(screen);
    auto* top = lv_obj_create(screen);
    auto* status_bar = lv_obj_create(screen);
    auto* bottom = lv_obj_create(screen);
    lv_obj_set_style_pad_all(bottom, 8, 0);
    auto* subtitle = lv_label_create(bottom);
    lv_label_set_text(subtitle, "Existing assistant subtitle");
    auto* status = lv_label_create(status_bar);
    auto* notification = lv_label_create(status_bar);
    auto* battery = lv_obj_create(screen);
    LeleAssistantLayout{background, top,    status_bar,   bottom,
                        subtitle,   status, notification, battery}
        .MakeResponsive();
    lv_obj_update_layout(screen);
    assert(lv_obj_get_width(background) == width && lv_obj_get_height(background) == height);
    assert(lv_obj_get_width(top) == width && lv_obj_get_width(bottom) == width);
    assert(lv_obj_get_width(subtitle) == lv_obj_get_content_width(bottom));
    assert(std::string(lv_label_get_text(subtitle)) == "Existing assistant subtitle");
    // Rotate the existing widgets instead of reconstructing them.
    lv_display_set_rotation(display, rotation == LV_DISPLAY_ROTATION_0 ? LV_DISPLAY_ROTATION_90
                                                                       : LV_DISPLAY_ROTATION_0);
    lv_obj_update_layout(screen);
    assert(lv_obj_get_width(background) == height && lv_obj_get_height(background) == width);
    assert(lv_obj_get_width(top) == height && lv_obj_get_width(bottom) == height);
    assert(lv_obj_get_width(subtitle) == lv_obj_get_content_width(bottom));
    for (auto* object : {background, top, status_bar, bottom, battery})
        lv_obj_delete(object);
    lv_display_set_rotation(display, rotation);

    // Theme, orientation, paired brightness buttons and Back must all remain
    // visible and hit-testable, particularly on the short landscape screen.
    auto* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(overlay);
    lv_obj_set_style_pad_all(overlay, 6, 0);
    lv_obj_set_style_pad_row(overlay, 4, 0);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    for (unsigned i = 0; i < 2; ++i) {
        auto* heading = lv_label_create(overlay);
        lv_obj_set_height(heading, 28);
    }
    auto* theme = CreateLeleButton(overlay, "Theme: Light", false);
    auto* orientation = CreateLeleButton(overlay, "Orientation: Landscape", false);
    auto* brightness = CreateSongListFooter(overlay);
    lv_obj_set_height(brightness, 32);
    auto* dim = CreateLeleButton(brightness, "Dim -", false);
    auto* brighten = CreateLeleButton(brightness, "Bright +", false);
    for (auto* button : {dim, brighten}) {
        lv_obj_set_width(button, 0);
        lv_obj_set_flex_grow(button, 1);
    }
    auto* back = CreateLeleButton(overlay, "Back", false);
    lv_obj_update_layout(overlay);
    for (auto* button : {theme, orientation, dim, brighten, back}) {
        lv_area_t area;
        lv_obj_get_coords(button, &area);
        assert(area.x1 >= 0 && area.x2 < width && area.y1 >= 0 && area.y2 < height);
        lv_point_t point{(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
        assert(lv_indev_search_obj(lv_layer_top(), &point) == button);
    }
    lv_obj_delete(overlay);

    // New main menu keeps four vertical rows by pairing Display and Songs.
    // Reserve the production 28px title and 48px message even with a small host font.
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(overlay);
    lv_obj_set_style_pad_all(overlay, 6, 0);
    lv_obj_set_style_pad_row(overlay, 4, 0);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    auto* menu_title = lv_label_create(overlay);
    lv_obj_set_height(menu_title, 28);
    auto* message = lv_label_create(overlay);
    lv_obj_set_size(message, lv_pct(100), 48);
    auto* usb = CreateLeleButton(overlay, "USB File Transfer", false);
    auto* wifi = CreateLeleButton(overlay, "Wi-Fi File Transfer", false);
    auto* menu_row = CreateSongListFooter(overlay);
    lv_obj_set_height(menu_row, 32);
    auto* display_button = CreateLeleButton(menu_row, "Display", false);
    auto* songs = CreateLeleButton(menu_row, "Songs", false);
    for (auto* button : {display_button, songs}) {
        lv_obj_set_width(button, 0);
        lv_obj_set_flex_grow(button, 1);
    }
    back = CreateLeleButton(overlay, "Back", false);
    lv_obj_update_layout(overlay);
    for (auto* button : {usb, wifi, display_button, songs, back}) {
        lv_area_t area;
        lv_obj_get_coords(button, &area);
        assert(area.x1 >= 0 && area.x2 < width && area.y1 >= 0 && area.y2 < height);
        lv_point_t point{(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
        assert(lv_indev_search_obj(lv_layer_top(), &point) == button);
    }
    // Upload session replaces the menu; the small status text and End button
    // remain readable/reachable in both orientations.
    lv_obj_clean(overlay);
    menu_title = lv_label_create(overlay);
    lv_obj_set_height(menu_title, 28);
    message = lv_label_create(overlay);
    lv_obj_set_width(message, lv_pct(100));
    lv_obj_set_height(message, 0);
    lv_obj_set_flex_grow(message, 1);
    lv_obj_set_style_text_font(message, LV_FONT_DEFAULT, 0);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_label_set_text(message,
                      "http://192.168.100.100:8080\nCode: 012345\nChanges: 100\n"
                      "64 MiB/file; 10-minute session\nManage SD files in browser.\n"
                      "End transfer to update Songs.");
    auto* end = CreateLeleButton(overlay, "End Wi-Fi Transfer", false);
    lv_obj_update_layout(overlay);
    assert(lv_obj_get_height(message) >= 7 * lv_font_get_line_height(LV_FONT_DEFAULT));
    lv_area_t end_area;
    lv_obj_get_coords(end, &end_area);
    assert(end_area.y2 < height);
    lv_point_t end_point{(end_area.x1 + end_area.x2) / 2, (end_area.y1 + end_area.y2) / 2};
    assert(lv_indev_search_obj(lv_layer_top(), &end_point) == end);
    lv_obj_delete(overlay);
}

int main() {
    lv_init();
    auto* display = lv_display_create(320, 240);
    auto* screen = lv_screen_active();
    lv_point_t center{160, 120};

    // Reproduce the original bug: creating the input before the full-screen
    // UI leaves it below later siblings and the center never hits the region.
    auto* early = EnsureAssistantTouchRegion(true, false, nullptr, Event, nullptr);
    auto* background = lv_obj_create(screen);
    lv_obj_remove_style_all(background);
    lv_obj_set_size(background, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(background, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_update_layout(screen);
    assert(lv_indev_search_obj(screen, &center) == background);
    lv_obj_delete(early);

    // Production lifecycle guard rejects the premature creation. Once the
    // screen exists, the region wins hit testing without changing rendering.
    assert(!EnsureAssistantTouchRegion(false, false, nullptr, Event, nullptr));
    auto* region = EnsureAssistantTouchRegion(true, false, nullptr, Event, nullptr);
    lv_obj_update_layout(screen);
    assert(lv_indev_search_obj(screen, &center) == region);
    lv_area_t area;
    lv_obj_get_coords(region, &area);
    assert(area.x1 == 0 && area.x2 == 319 && area.y1 == 0 && area.y2 == 239);
    lv_point_t points[] = {{0, 0},   {319, 0},   {0, 239}, {319, 239}, {160, 120},
                           {160, 0}, {160, 239}, {0, 120}, {319, 120}};
    for (auto& point : points)
        assert(lv_indev_search_obj(screen, &point) == region);
    assert(lv_obj_get_style_bg_opa(region, LV_PART_MAIN) == LV_OPA_TRANSP);
    const auto children = lv_obj_get_child_count(screen);
    assert(EnsureAssistantTouchRegion(true, false, region, Event, nullptr) == region);
    assert(lv_obj_get_child_count(screen) == children);
    assert(!EnsureAssistantTouchRegion(true, true, nullptr, Event, nullptr));

    // The normal menu lives on the higher layer and must intercept the touch.
    auto* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(overlay);
    lv_obj_update_layout(lv_layer_top());
    for (auto& point : points)
        assert(lv_indev_search_obj(lv_layer_top(), &point) == overlay);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    assert(!lv_indev_search_obj(lv_layer_top(), &center));
    assert(lv_indev_search_obj(screen, &center) == region);
    lv_obj_delete(overlay);

    for (auto rotation : {LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_0}) {
        CheckOrientation(display, rotation, region);
        const int width = lv_display_get_horizontal_resolution(display);
        const int height = lv_display_get_vertical_resolution(display);
        // Production song-list layout: five rows and four controls in both sizes.
        overlay = lv_obj_create(lv_layer_top());
        lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
        lv_obj_center(overlay);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_set_style_pad_all(overlay, 6, 0);
        lv_obj_set_style_pad_row(overlay, 4, 0);
        lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
        auto* title = lv_label_create(overlay);
        lv_label_set_text(title, "Songs 1/205 (1024)");
        lv_obj_set_height(title, 28);
        auto* rows = CreateSongListRows(overlay);
        lv_obj_t* buttons[5];
        for (auto& button : buttons) {
            button = CreateLeleButton(rows, "Long song name for layout testing", true);
            ConfigureSongListButton(button, nullptr);
        }
        auto* footer = CreateSongListFooter(overlay);
        const char* controls[] = {"Prev", "Next", "Stop", "Back"};
        for (const char* name : controls) {
            auto* button = CreateLeleButton(footer, name, true);
            lv_obj_set_width(button, 0);
            lv_obj_set_height(button, 30);
            lv_obj_set_flex_grow(button, 1);
        }
        lv_obj_update_layout(overlay);
        lv_area_t footer_area;
        lv_obj_get_coords(footer, &footer_area);
        assert(footer_area.y2 < height && footer_area.y1 > 0);
        for (auto* button : buttons) {
            lv_area_t area;
            lv_obj_get_coords(button, &area);
            assert(area.y1 >= 0 && area.y2 < footer_area.y1);
            assert(area.x1 >= 0 && area.x2 < width);
            assert(lv_obj_get_height(button) == 30);
            auto* label = lv_obj_get_child(button, 0);
            assert(lv_obj_get_height(label) == lv_font_get_line_height(LV_FONT_DEFAULT));
            assert(lv_label_get_long_mode(label) == LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_point_t point{(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
            assert(lv_indev_search_obj(lv_layer_top(), &point) == button);
        }
        for (unsigned i = 0; i < 4; ++i) {
            auto* button = lv_obj_get_child(footer, i);
            lv_area_t area;
            lv_obj_get_coords(button, &area);
            assert(area.y2 < height && area.x1 >= 0 && area.x2 < width);
            assert(lv_obj_get_width(button) >= 48);
        }
        lv_obj_delete(overlay);
        CheckSongScrolling(width);
    }
    lv_display_delete(display);
    lv_deinit();
}
