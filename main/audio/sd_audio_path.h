#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace SdAudio {
inline bool SafePath(const std::string& path, const std::string& root = "/sdcard") {
    if (path.compare(0, root.size() + 1, root + "/") != 0 || path.size() > 240 ||
        path.find('\\') != std::string::npos || path.find('\0') != std::string::npos ||
        path.find("//") != std::string::npos)
        return false;
    size_t start = root.size() + 1;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part == "." || part == "..")
            return false;
        if (end == std::string::npos)
            return true;
        start = end + 1;
    }
    return false;
}
inline std::string Extension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "";
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}
inline bool Playable(const std::string& path) { return Extension(path) == ".mp3"; }
inline bool Candidate(const std::string& path) { return Playable(path); }
}  // namespace SdAudio
