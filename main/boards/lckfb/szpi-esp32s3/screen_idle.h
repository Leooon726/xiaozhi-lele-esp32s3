#pragma once
#include <cstdint>

// Main-task clock only. Hardware state changes are committed separately.
class LeleScreenIdle {
public:
    static constexpr int64_t kTimeoutUs = 60LL * 1000000;

    void Activity(int64_t now) {
        last_activity_ = now;
        initialized_ = true;
    }
    bool Due(int64_t now, bool keep_awake) {
        if (!initialized_ || keep_awake || now < last_activity_)
            Activity(now);
        return now - last_activity_ >= kTimeoutUs;
    }

private:
    int64_t last_activity_ = 0;
    bool initialized_ = false;
};
