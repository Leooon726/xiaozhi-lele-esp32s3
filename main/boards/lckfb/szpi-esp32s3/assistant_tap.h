#pragma once
#include <cstdint>

// Board-local tap recognition, independent of LVGL and the audio pipeline.
class AssistantTap {
public:
    void Begin(int32_t x, int32_t y, uint32_t now, bool enabled) {
        active_ = enabled;
        moved_ = false;
        x_ = x;
        y_ = y;
        started_ = now;
    }
    void Move(int32_t x, int32_t y) {
        const int64_t dx = int64_t(x) - x_;
        const int64_t dy = int64_t(y) - y_;
        if (dx < -12 || dx > 12 || dy < -12 || dy > 12)
            moved_ = true;
    }
    bool End(int32_t x, int32_t y, uint32_t now, bool enabled) {
        Move(x, y);
        const uint32_t duration = now - started_;
        const bool accepted = active_ && enabled && !moved_ && duration >= 20 && duration < 500 &&
                              (!has_last_ || uint32_t(now - last_) >= 350);
        active_ = false;
        if (accepted) {
            last_ = now;
            has_last_ = true;
        }
        return accepted;
    }
    void Cancel() { active_ = false; }
    bool Stationary() const { return active_ && !moved_; }

private:
    bool active_ = false;
    bool moved_ = false;
    bool has_last_ = false;
    int32_t x_ = 0, y_ = 0;
    uint32_t started_ = 0, last_ = 0;
};
