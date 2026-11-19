#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "music_paths.h"

namespace TfMusic {
constexpr size_t kBrowserPageSize = 4;
struct BrowserPage {
    std::array<std::string, kBrowserPageSize> paths;
    size_t offset = 0, count = 0, total = 0;
    uint32_t revision = 0;
    bool ready = false, scanning = false, truncated = false, blocked = false;
    std::string error;
};

inline BrowserPage CopyBrowserPage(const MusicPaths& library, size_t requested, uint32_t revision) {
    BrowserPage page;
    page.total = library.size();
    page.revision = revision;
    if (page.total) {
        const size_t last = (page.total - 1) / kBrowserPageSize * kBrowserPageSize;
        page.offset = std::min(requested / kBrowserPageSize * kBrowserPageSize, last);
        page.count = std::min(kBrowserPageSize, page.total - page.offset);
        for (size_t row = 0; row < page.count; ++row)
            page.paths[row] = library[page.offset + row].view();
    }
    return page;
}

inline bool HasBrowserPath(const MusicPaths& library, const std::string& path) {
    for (const auto& entry : library)
        if (entry.view() == path)
            return true;
    return false;
}

inline std::string BrowserName(const std::string& path) {
    const auto slash = path.find_last_of('/');
    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    if (name.size() > 4)
        name.resize(name.size() - 4);  // Indexed paths are MP3 only.
    if (slash != std::string::npos && slash > 8)
        name += " [" + path.substr(8, slash - 8) + "]";
    return name;
}
}  // namespace TfMusic
