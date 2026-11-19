#pragma once

#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include "music_library.h"

namespace TfMusic {
// Portable, bounded format. Never trust file lengths/counts from the card.
inline uint32_t HashByte(uint32_t hash, uint8_t value) { return (hash ^ value) * 16777619u; }
inline void Put32(uint8_t* out, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out[i] = value >> (8 * i);
}
inline uint32_t Get32(const uint8_t* in) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= uint32_t(in[i]) << (8 * i);
    return value;
}
inline uint32_t IndexHash(const Index& index, uint32_t generation) {
    uint8_t metadata[16] = {};
    Put32(metadata, generation);
    Put32(metadata + 4, index.paths.size());
    Put32(metadata + 8, index.path_bytes);
    Put32(metadata + 12, index.truncated ? 1 : 0);
    uint32_t hash = 2166136261u;
    for (uint8_t value : metadata)
        hash = HashByte(hash, value);
    for (const auto& path : index.paths) {
        hash = HashByte(hash, path.size() & 0xff);
        hash = HashByte(hash, path.size() >> 8);
        for (uint8_t value : path)
            hash = HashByte(hash, value);
    }
    return hash;
}
inline bool ValidIndex(const Index& index, const std::string& root) {
    if (index.io_error || index.paths.size() > kMaxTracks || index.path_bytes > kMaxPathBytes)
        return false;
    size_t bytes = 0;
    std::string previous;
    for (const auto& path : index.paths) {
        if (!ValidPath(path, root) || (!previous.empty() && path <= previous))
            return false;
        bytes += path.size() + 1;
        previous = path;
    }
    return bytes == index.path_bytes;
}
inline bool LoadCache(const std::string& file, const std::string& root, Index& result,
                      uint32_t& generation) {
    FILE* stream = std::fopen(file.c_str(), "rb");
    if (!stream)
        return false;
    uint8_t header[28];
    bool ok = std::fread(header, 1, sizeof(header), stream) == sizeof(header) &&
              std::memcmp(header, "LLMIDX02", 8) == 0;
    Index candidate;
    uint32_t count = 0, gen = 0, expected_hash = 0;
    if (ok) {
        gen = Get32(header + 8);
        count = Get32(header + 12);
        candidate.path_bytes = Get32(header + 16);
        const uint32_t flags = Get32(header + 20);
        candidate.truncated = flags == 1;
        expected_hash = Get32(header + 24);
        ok = count <= kMaxTracks && candidate.path_bytes <= kMaxPathBytes && flags <= 1;
    }
    size_t bytes = 0;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint8_t length[2];
        char path[241];
        ok = std::fread(length, 1, 2, stream) == 2;
        size_t size = ok ? (size_t(length[0]) | (size_t(length[1]) << 8)) : 0;
        ok = ok && size > 0 && size <= 240 && bytes + size + 1 <= kMaxPathBytes;
        if (!ok)
            break;
        ok = std::fread(path, 1, size, stream) == size;
        if (!ok)
            break;
        candidate.paths.emplace_back(path, size);
        bytes += size + 1;
    }
    if (ok)
        ok = std::fgetc(stream) == EOF && !std::ferror(stream);
    std::fclose(stream);
    if (!ok || !ValidIndex(candidate, root) || IndexHash(candidate, gen) != expected_hash)
        return false;
    result = std::move(candidate);
    generation = gen;
    return true;
}

inline bool NewerGeneration(uint32_t candidate, uint32_t previous) {
    const uint32_t distance = candidate - previous;
    return distance != 0 && distance < 0x80000000u;
}

inline int LoadLatestCache(const std::string& first, const std::string& second,
                           const std::string& root, Index& result, uint32_t& generation) {
    const std::string* files[] = {&first, &second};
    int selected = -1;
    for (int slot = 0; slot < 2; ++slot) {
        Index candidate;
        uint32_t candidate_generation = 0;
        if (LoadCache(*files[slot], root, candidate, candidate_generation) &&
            (selected < 0 || NewerGeneration(candidate_generation, generation))) {
            result = std::move(candidate);
            generation = candidate_generation;
            selected = slot;
        }
    }
    return selected;
}

// Write the inactive slot; the previous valid slot survives interrupted writes.
// No rename-overwrite atomicity assumption is made about FAT.
inline bool SaveCache(const std::string& file, const std::string& root, const Index& index,
                      uint32_t generation, const std::function<void()>& checkpoint = {}) {
    if (!ValidIndex(index, root))
        return false;
    if (checkpoint)
        checkpoint();
    FILE* stream = std::fopen(file.c_str(), "wb");
    if (!stream)
        return false;
    uint8_t header[28] = {};
    std::memcpy(header, "LLMIDX02", 8);
    Put32(header + 8, generation);
    Put32(header + 12, index.paths.size());
    Put32(header + 16, index.path_bytes);
    Put32(header + 20, index.truncated ? 1 : 0);
    Put32(header + 24, IndexHash(index, generation));
    bool ok = std::fwrite(header, 1, sizeof(header), stream) == sizeof(header);
    for (const auto& path : index.paths) {
        if (!ok)
            break;
        if (checkpoint)
            checkpoint();
        uint8_t length[2] = {uint8_t(path.size()), uint8_t(path.size() >> 8)};
        ok = std::fwrite(length, 1, 2, stream) == 2 &&
             std::fwrite(path.data(), 1, path.size(), stream) == path.size();
    }
    if (ok)
        ok = std::fflush(stream) == 0 && fsync(fileno(stream)) == 0;
    if (std::fclose(stream) != 0)
        ok = false;
    // Read back with the same checksum/format validation used at boot.
    Index verified;
    uint32_t verified_generation = 0;
    return ok && LoadCache(file, root, verified, verified_generation) &&
           verified_generation == generation && verified.paths == index.paths &&
           verified.truncated == index.truncated;
}

class IdleScanPolicy {
public:
    static constexpr uint32_t kQuietPeriodMs = 10000;
    bool CanRun(bool manual, bool idle, uint32_t now) {
        if (!idle)
            quiet_ = false;
        if (manual)
            return true;
        if (!idle)
            return false;
        if (!quiet_) {
            quiet_ = true;
            quiet_since_ = now;
        }
        return uint32_t(now - quiet_since_) >= kQuietPeriodMs;
    }

private:
    bool quiet_ = false;
    uint32_t quiet_since_ = 0;
};
}  // namespace TfMusic
