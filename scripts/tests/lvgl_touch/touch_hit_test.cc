#include <cassert>
#include "assistant_touch_region.h"

static void Event(lv_event_t*) {}

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
    lv_display_delete(display);
    lv_deinit();
}
