// Adapted from jianglixi-oss/xiaozhi-nas-tf-music (MIT); see MUSIC_PORT.md.
#pragma once

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <functional>
#include <string>
#include <vector>
#include "music_paths.h"
#include "sd_audio_path.h"

namespace TfMusic {
inline bool ValidPath(std::string_view path, const std::string& root = "/sdcard") {
    const std::string value(path);
    return SdAudio::SafePath(value, root) && SdAudio::Candidate(value);
}

inline bool Matches(std::string_view path, std::string_view query) {
    if (path.size() > 240 || query.size() > path.size())
        return false;
    // Bounded stack buffers avoid heap churn during a 1024-track MCP search.
    // Fold only ASCII; preserve Chinese UTF-8 bytes exactly.
    char folded_path[240], folded_query[240];
    auto fold = [](unsigned char c) -> char { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    std::transform(path.begin(), path.end(), folded_path, fold);
    std::transform(query.begin(), query.end(), folded_query, fold);
    return std::string_view(folded_path, path.size())
               .find(std::string_view(folded_query, query.size())) != std::string_view::npos;
}

struct Index {
    MusicPaths paths;
    bool truncated = false;
    bool io_error = false;
    bool memory_error = false;
    int visited = 0;
    size_t path_bytes = 0;
};

inline bool SkipEntry(const std::string& name) {
    return name == "." || name == ".." || name.compare(0, 2, "._") == 0 ||
           name == ".Spotlight-V100" || name == ".Trashes" || name == ".fseventsd" ||
           name == "System Volume Information" || name == "$RECYCLE.BIN";
}

// Iterative traversal: arbitrary nesting within path/entry/memory limits,
// with only one directory open at a time and no recursive task-stack growth.
inline void Scan(const std::string& root, const std::string& directory, int, Index& result,
                 const std::function<void()>& checkpoint = {}) {
    std::vector<std::string> pending{directory};
    size_t pending_bytes = directory.size() + 1;
    while (!pending.empty() && result.visited < kMaxEntries) {
        std::string current = std::move(pending.back());
        pending.pop_back();
        pending_bytes -= current.size() + 1;
        if (checkpoint)
            checkpoint();
        DIR* dir = opendir(current.c_str());
        if (!dir) {
            result.io_error = true;
            continue;
        }
        while (true) {
            if (checkpoint)
                checkpoint();
            errno = 0;
            auto* entry = readdir(dir);
            if (!entry) {
                if (errno != 0)
                    result.io_error = true;
                break;
            }
            if (SkipEntry(entry->d_name))
                continue;
            if (++result.visited > kMaxEntries) {
                result.truncated = true;
                break;
            }
            std::string path = current + "/" + entry->d_name;
            if (!SdAudio::SafePath(path, root)) {
                result.truncated = true;
                continue;
            }
            struct stat st;
            if (checkpoint)
                checkpoint();
            if (stat(path.c_str(), &st) != 0) {
                result.io_error = true;
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                if (pending.size() >= kMaxPendingDirectories ||
                    pending_bytes + path.size() + 1 > kMaxPendingPathBytes) {
                    result.truncated = true;
                } else {
                    pending_bytes += path.size() + 1;
                    pending.push_back(std::move(path));
                }
            } else if (S_ISREG(st.st_mode) && ValidPath(path, root)) {
                if (result.paths.size() >= kMaxTracks ||
                    result.path_bytes + path.size() + 1 > kMaxPathBytes) {
                    result.truncated = true;
                    continue;
                }
                if (!result.paths.push_back(path)) {
                    result.memory_error = true;
                    break;
                }
                result.path_bytes += path.size() + 1;
            }
        }
        if (closedir(dir) != 0)
            result.io_error = true;
        if (result.memory_error)
            return;
    }
    if (!pending.empty())
        result.truncated = true;
}
}  // namespace TfMusic
