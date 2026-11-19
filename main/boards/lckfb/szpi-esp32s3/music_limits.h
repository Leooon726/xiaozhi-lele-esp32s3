#pragma once
#include <cstddef>

namespace TfMusic {
constexpr size_t kMaxTracks = 1024;
constexpr size_t kMaxPathBytes = 256 * 1024;
constexpr int kMaxEntries = 16384;
constexpr size_t kMaxPendingDirectories = 128;
constexpr size_t kMaxPendingPathBytes = 16 * 1024;
}  // namespace TfMusic
