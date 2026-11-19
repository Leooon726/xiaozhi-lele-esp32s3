"""Exercise production stream control with codec/PSRAM test doubles, not MP3 fidelity."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MusicStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        (folder / "esp_log.h").write_text(r'''
#pragma once
#include <cstdio>
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
''')
        (folder / "esp_heap_caps.h").write_text(r'''
#pragma once
#include <cstdlib>
inline int live_buffers = 0;
inline bool fail_alloc = false;
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4;
inline size_t heap_caps_get_free_size(int) { return 100000; }
inline size_t heap_caps_get_largest_free_block(int) { return 40000; }
inline void* heap_caps_malloc(size_t n, int) {
    if (fail_alloc) return nullptr;
    void* p = malloc(n); if (p) ++live_buffers; return p;
}
inline void heap_caps_free(void* p) { if (p) --live_buffers; free(p); }
''')
        (folder / "esp_mp3_dec.h").write_text(r'''
#pragma once
#include <cstdint>
#include <cstring>
using esp_audio_err_t = int;
constexpr int ESP_AUDIO_ERR_OK = 0;
struct esp_audio_dec_in_raw_t { uint8_t* buffer; uint32_t len, consumed; };
struct esp_audio_dec_out_frame_t { uint8_t* buffer; uint32_t len, decoded_size, needed_size; };
struct esp_audio_dec_info_t { uint32_t bits_per_sample, channel, sample_rate; };
inline int open_decoders = 0;
inline bool oversized_output = false;
inline int esp_mp3_dec_open(void*, int, void** out) { ++open_decoders; *out = &open_decoders; return 0; }
inline int esp_mp3_dec_close(void*) { --open_decoders; return 0; }
inline int esp_mp3_dec_decode(void*, esp_audio_dec_in_raw_t* raw,
    esp_audio_dec_out_frame_t* frame, esp_audio_dec_info_t* info) {
    if (raw->len < 4 || memcmp(raw->buffer, "FRAM", 4)) return -1;
    raw->consumed = 4;
    int16_t pcm[] = {1000, -1000, 500, 500};
    memcpy(frame->buffer, pcm, sizeof(pcm));
    frame->decoded_size = oversized_output ? frame->len + 2 : sizeof(pcm);
    *info = {16, 2, 24000};
    return 0;
}
''')
        (folder / "esp_ae_rate_cvt.h").write_text(r'''
#pragma once
#include <cstdint>
using esp_ae_rate_cvt_handle_t = void*;
using esp_ae_sample_t = void*;
constexpr int ESP_AE_ERR_OK = 0, ESP_AE_RATE_CVT_PERF_TYPE_SPEED = 0;
struct esp_ae_rate_cvt_cfg_t { int src_rate, dest_rate, channel, bits_per_sample, complexity, perf_type; };
// These tests stay at native 24 kHz. Real resampling is verified by target build/hardware.
inline int esp_ae_rate_cvt_open(esp_ae_rate_cvt_cfg_t*, void**) { return -1; }
inline int esp_ae_rate_cvt_close(void*) { return 0; }
inline int esp_ae_rate_cvt_get_max_out_sample_num(void*, uint32_t, uint32_t*) { return -1; }
inline int esp_ae_rate_cvt_process(void*, void*, uint32_t, void*, uint32_t*) { return -1; }
''')
        driver = folder / "driver.cc"
        driver.write_text(r'''
#include "music_stream.h"
#include "esp_heap_caps.h"
#include "esp_mp3_dec.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    std::string mode = argv[1];
    std::string data = "FRAMFRAM";
    bool expected = true, cancel = false;
    if (mode == "large_id3") {
        data = std::string("ID3\x04\x00\x00\x00\x01\x1c\x20", 10) + std::string(20000, 'x') + data;
    } else if (mode == "bad_id3") {
        data = std::string("ID3\x04\x00\x00\x80\x00\x00\x00", 10) + data; expected = false;
    } else if (mode == "truncated_id3") {
        data = std::string("ID3\x04\x00\x00\x00\x01\x1c\x20", 10); expected = false;
    } else if (mode == "corrupt") { data = "broken MP3"; expected = false;
    } else if (mode == "empty") { data.clear(); expected = false;
    } else if (mode == "read_error" || mode == "sink_cancel" || mode == "cancel_read") {
        expected = false;
    } else if (mode == "alloc_fail") { fail_alloc = true; expected = false;
    } else if (mode == "oversized_output") { oversized_output = true; expected = false; }
    size_t pos = 0, chunks = 0;
    auto read = [&](uint8_t* out, size_t capacity) {
        if (mode == "read_error") return -1;
        // Fragmented reads, including ID3 tags much larger than the input buffer.
        size_t n = std::min({capacity, size_t(17), data.size() - pos});
        memcpy(out, data.data() + pos, n); pos += n;
        if (mode == "cancel_read") cancel = true;
        return static_cast<int>(n);
    };
    bool ok = DecodeMusic(read, [&]() { return cancel; }, 24000,
        [&](std::vector<int16_t>&& pcm) {
            assert(pcm == std::vector<int16_t>({0, 500}));
            ++chunks;
            return mode != "sink_cancel";
        });
    assert(ok == expected);
    if (expected) assert(chunks == 2);
    assert(live_buffers == 0);
    assert(open_decoders == 0);
    std::cout << mode << ": passed\n";
}
''')
        cls.exe = folder / "stream-test"
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-I" + str(folder), "-I" + str(ROOT / "main/audio"),
            str(ROOT / "main/audio/music_stream.cc"), str(driver), "-o", str(cls.exe),
        ], check=True, capture_output=True, text=True)

    def test_stream_errors_cancellation_id3_and_buffer_cleanup(self):
        for mode in ["normal", "large_id3", "bad_id3", "truncated_id3", "corrupt",
                     "empty", "read_error", "sink_cancel", "cancel_read", "alloc_fail",
                     "oversized_output"]:
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.exe), mode], check=True, capture_output=True, text=True)
                stages = {
                    "bad_id3": "id3_header", "truncated_id3": "id3_read",
                    "corrupt": "decode_frame", "empty": "empty_input",
                    "read_error": "initial_read", "sink_cancel": "pcm_queue",
                    "alloc_fail": "buffers", "oversized_output": "invalid_decoded_size",
                }
                if mode in stages:
                    self.assertIn("stage=" + stages[mode], result.stderr)


if __name__ == "__main__":
    unittest.main()
