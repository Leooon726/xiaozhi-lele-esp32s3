#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>
#include "music_limits.h"
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace TfMusic {
// Fixed-size records avoid one internal-SRAM allocation per filename. Only the
// occupied capacity is allocated, explicitly in PSRAM on the device. Failed
// growth preserves the previous allocation; there is no SRAM fallback or throw.
struct MusicPath {
    uint16_t length = 0;
    char text[241] = {};
    size_t size() const { return length; }
    const char* data() const { return text; }
    const char* c_str() const { return text; }
    std::string_view view() const { return {text, length}; }
    friend bool operator<(const MusicPath& a, const MusicPath& b) { return a.view() < b.view(); }
    friend bool operator==(const MusicPath& a, const MusicPath& b) { return a.view() == b.view(); }
    friend bool operator==(const MusicPath& a, std::string_view b) { return a.view() == b; }
};
static_assert(sizeof(MusicPath) == 244);

class MusicPaths {
public:
    MusicPaths() = default;
    MusicPaths(const MusicPaths&) = delete;
    MusicPaths& operator=(const MusicPaths&) = delete;
    MusicPaths(MusicPaths&& other) noexcept { *this = std::move(other); }
    MusicPaths& operator=(MusicPaths&& other) noexcept {
        if (this != &other) {
            Release();
            paths_ = std::exchange(other.paths_, nullptr);
            count_ = std::exchange(other.count_, 0);
            capacity_ = std::exchange(other.capacity_, 0);
        }
        return *this;
    }
    ~MusicPaths() { Release(); }

    bool reserve(size_t capacity) {
        if (capacity > kMaxTracks)
            return false;
        if (capacity <= capacity_)
            return true;
#ifdef ESP_PLATFORM
        void* memory = heap_caps_realloc(paths_, capacity * sizeof(MusicPath),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        void* memory = std::realloc(paths_, capacity * sizeof(MusicPath));
#endif
        if (!memory)
            return false;
        paths_ = static_cast<MusicPath*>(memory);
        capacity_ = capacity;
        return true;
    }
    bool emplace_back(const char* path, size_t length) {
        if (length == 0 || length > 240 || count_ >= kMaxTracks)
            return false;
        // Copy before realloc, including when the input aliases an existing record.
        MusicPath value;
        value.length = static_cast<uint16_t>(length);
        std::memcpy(value.text, path, length);
        if (count_ == capacity_) {
            const size_t next = capacity_ ? capacity_ * 2 : 16;
            if (!reserve(next < kMaxTracks ? next : kMaxTracks))
                return false;
        }
        new (paths_ + count_++) MusicPath(value);
        return true;
    }
    bool push_back(const MusicPath& path) { return emplace_back(path.data(), path.size()); }
    bool push_back(std::string_view path) { return emplace_back(path.data(), path.size()); }
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    size_t storage_bytes() const { return capacity_ * sizeof(MusicPath); }
    MusicPath& operator[](size_t index) { return paths_[index]; }
    const MusicPath& operator[](size_t index) const { return paths_[index]; }
    MusicPath* begin() { return paths_ ? paths_ : &empty_; }
    MusicPath* end() { return begin() + count_; }
    const MusicPath* begin() const { return paths_ ? paths_ : &empty_; }
    const MusicPath* end() const { return begin() + count_; }
    friend bool operator==(const MusicPaths& a, const MusicPaths& b) {
        if (a.count_ != b.count_)
            return false;
        for (size_t i = 0; i < a.count_; ++i)
            if (!(a[i] == b[i]))
                return false;
        return true;
    }

private:
    void Release() {
#ifdef ESP_PLATFORM
        heap_caps_free(paths_);
#else
        std::free(paths_);
#endif
    }
    MusicPath* paths_ = nullptr;
    size_t count_ = 0;
    size_t capacity_ = 0;
    inline static MusicPath empty_{};
};
}  // namespace TfMusic
