#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace TfMusic {
enum class ReadCheckStatus { Complete, Cancelled, ReadError, TooLarge, HashError };
struct ReadCheckResult {
    ReadCheckStatus status;
    size_t bytes;
};

// Bounded streaming control only. The caller owns the file/hash/buffer and closes
// them before releasing the SD ownership gate. No retries or writes occur here.
template <class Read, class Update, class Cancelled>
ReadCheckResult CheckReadStream(Read read, Update update, Cancelled cancelled, uint8_t* buffer,
                                size_t capacity, size_t max_bytes) {
    size_t total = 0;
    if (!buffer || capacity == 0 || max_bytes == std::numeric_limits<size_t>::max())
        return {ReadCheckStatus::ReadError, 0};
    while (true) {
        if (cancelled())
            return {ReadCheckStatus::Cancelled, total};
        // One extra byte distinguishes an exact-limit file from an oversized one.
        const size_t requested = std::min(capacity, max_bytes - total + 1);
        const int count = read(buffer, requested);
        if (count < 0 || static_cast<size_t>(count) > requested)
            return {ReadCheckStatus::ReadError, total};
        if (cancelled())
            return {ReadCheckStatus::Cancelled, total};
        if (count == 0)
            return {ReadCheckStatus::Complete, total};
        if (static_cast<size_t>(count) > max_bytes - total)
            return {ReadCheckStatus::TooLarge, total};
        if (!update(buffer, static_cast<size_t>(count)))
            return {ReadCheckStatus::HashError, total};
        total += static_cast<size_t>(count);
    }
}
}  // namespace TfMusic
