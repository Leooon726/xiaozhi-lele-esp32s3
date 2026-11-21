#pragma once

namespace TfMusic {
// Board-local stability candidate. Assistant and isolated USB MSC use the same
// ceiling so repeated-read comparisons do not silently change SD timings.
// Previous SDMMC_FREQ_DEFAULT was 20 MHz. Hardware acceptance is still pending.
constexpr int kSdClockKhz = 5000;
}  // namespace TfMusic
