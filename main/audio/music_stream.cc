// Adapted from jianglixi-oss/xiaozhi-nas-tf-music (MIT); see MUSIC_PORT.md.
#include "music_stream.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include "esp_ae_rate_cvt.h"
#include "esp_mp3_dec.h"

bool DecodeMusic(const std::function<int(uint8_t*, size_t)>& read,
                 const std::function<bool()>& cancelled, int output_rate,
                 const std::function<bool(std::vector<int16_t>&&)>& sink) {
    constexpr const char* TAG = "MusicDecoder";
    auto failure = [&](const char* stage, int error = 0) {
        ESP_LOGE(
            TAG, "Failed: stage=%s error=%d; free internal=%u largest internal=%u free PSRAM=%u",
            stage, error, unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        return false;
    };
    constexpr size_t input_size = 8192;
    constexpr size_t decoded_samples = 2304;
    // Keep bulk decode buffers off internal SRAM used by AFE and task stacks.
    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> input(
        static_cast<uint8_t*>(heap_caps_malloc(input_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        heap_caps_free);
    std::unique_ptr<int16_t, decltype(&heap_caps_free)> decoded(
        static_cast<int16_t*>(heap_caps_malloc(decoded_samples * sizeof(int16_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        heap_caps_free);
    if (!input || !decoded) {
        return failure("buffers");
    }
    size_t buffered = 0;
    bool eof = false, ok = true, had_audio = false;

    // Buffer a bounded chunk; large ID3 tags are skipped without loading the file.
    while (buffered < input_size && !eof && !cancelled()) {
        int n = read(input.get() + buffered, input_size - buffered);
        if (n < 0)
            return failure("initial_read", n);
        if (n == 0) {
            eof = true;
            break;
        }
        buffered += n;
    }
    if (buffered == 0)
        return failure("empty_input");

    // ID3v2 tags can be much larger than the decode buffer. Skip incrementally.
    if (buffered >= 10 && std::memcmp(input.get(), "ID3", 3) == 0) {
        uint32_t skip = 0;
        bool valid = true;
        for (int i = 6; i < 10; ++i) {
            if (input.get()[i] & 0x80) {
                valid = false;
                break;
            }
            skip = (skip << 7) | input.get()[i];
        }
        if (input.get()[5] & 0x10)
            skip += 10;  // optional ID3 footer
        skip += 10;      // tag header
        if (!valid)
            return failure("id3_header");
        ESP_LOGI(TAG, "Skipping ID3v2: %u bytes", unsigned(skip));
        if (skip >= buffered) {
            uint32_t remaining = skip - static_cast<uint32_t>(buffered);
            buffered = 0;
            while (remaining && !cancelled()) {
                size_t chunk = std::min<size_t>(remaining, input_size);
                int n = read(input.get(), chunk);
                if (n <= 0)
                    return failure("id3_read", n);
                remaining -= static_cast<uint32_t>(n);
            }
        } else {
            std::memmove(input.get(), input.get() + skip, buffered - skip);
            buffered -= skip;
        }
    }

    void* decoder = nullptr;
    auto open_error = esp_mp3_dec_open(nullptr, 0, &decoder);
    if (open_error != ESP_AUDIO_ERR_OK)
        return failure("decoder_open", open_error);
    ESP_LOGI(TAG, "MP3 decoder opened");

    esp_ae_rate_cvt_handle_t resampler = nullptr;
    int previous_rate = 0;

    while (ok && !cancelled()) {
        while (!eof && buffered < input_size && !cancelled()) {
            int n = read(input.get() + buffered, input_size - buffered);
            if (n < 0) {
                failure("stream_read", n);
                ok = false;
                break;
            }
            eof = n == 0;
            buffered += n;
        }
        if (!ok || cancelled())
            break;
        if (buffered == 0)
            break;
        if (eof && buffered == 128 && std::memcmp(input.get(), "TAG", 3) == 0)
            break;  // ID3v1 trailer
        esp_audio_dec_in_raw_t raw = {};
        raw.buffer = input.get();
        raw.len = buffered;
        esp_audio_dec_out_frame_t frame = {};
        frame.buffer = reinterpret_cast<uint8_t*>(decoded.get());
        frame.len = decoded_samples * sizeof(int16_t);
        esp_audio_dec_info_t info = {};
        auto err = esp_mp3_dec_decode(decoder, &raw, &frame, &info);
        if (raw.consumed > buffered) {
            failure("invalid_consumed_size");
            ok = false;
            break;
        }
        buffered -= raw.consumed;
        std::memmove(input.get(), input.get() + raw.consumed, buffered);
        if (err != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "Decode: buffered=%u consumed=%u decoded=%u needed=%u eof=%d",
                     unsigned(buffered), unsigned(raw.consumed), unsigned(frame.decoded_size),
                     unsigned(frame.needed_size), eof);
            failure("decode_frame", err);
            ok = false;
            break;
        }
        if (frame.decoded_size) {
            if (frame.decoded_size > decoded_samples * sizeof(int16_t)) {
                failure("invalid_decoded_size");
                ok = false;
                break;
            }
            if (info.bits_per_sample != 16 || info.channel < 1 || info.channel > 2 ||
                info.sample_rate < 8000 || info.sample_rate > 48000) {
                ESP_LOGE(TAG, "Unsupported PCM: rate=%u bits=%u channels=%u",
                         unsigned(info.sample_rate), unsigned(info.bits_per_sample),
                         unsigned(info.channel));
                failure("pcm_format");
                ok = false;
                break;
            }
            size_t samples = frame.decoded_size / sizeof(int16_t) / info.channel;
            std::vector<int16_t> mono(samples);
            for (size_t i = 0; i < samples; ++i) {
                int sum = decoded.get()[i * info.channel];
                if (info.channel == 2)
                    sum += decoded.get()[i * 2 + 1];
                mono[i] = sum / info.channel;
            }
            if (previous_rate != static_cast<int>(info.sample_rate)) {
                ESP_LOGI(TAG, "PCM format: rate=%u bits=%u channels=%u; output=%d",
                         unsigned(info.sample_rate), unsigned(info.bits_per_sample),
                         unsigned(info.channel), output_rate);
                if (resampler) {
                    esp_ae_rate_cvt_close(resampler);
                    resampler = nullptr;
                }
                previous_rate = info.sample_rate;
                if (previous_rate != output_rate) {
                    esp_ae_rate_cvt_cfg_t cfg = {};
                    cfg.src_rate = previous_rate;
                    cfg.dest_rate = output_rate;
                    cfg.channel = 1;
                    cfg.bits_per_sample = 16;
                    cfg.complexity = 2;
                    cfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
                    auto rate_error = esp_ae_rate_cvt_open(&cfg, &resampler);
                    if (rate_error != ESP_AE_ERR_OK) {
                        failure("resampler_open", rate_error);
                        ok = false;
                        break;
                    }
                }
            }
            if (resampler) {
                uint32_t capacity = 0;
                auto capacity_error =
                    esp_ae_rate_cvt_get_max_out_sample_num(resampler, samples, &capacity);
                if (capacity_error != ESP_AE_ERR_OK || capacity == 0) {
                    failure("resampler_capacity", capacity_error);
                    ok = false;
                    break;
                }
                std::vector<int16_t> converted(capacity);
                auto process_error = esp_ae_rate_cvt_process(
                    resampler, reinterpret_cast<esp_ae_sample_t>(mono.data()), samples,
                    reinterpret_cast<esp_ae_sample_t>(converted.data()), &capacity);
                if (process_error != ESP_AE_ERR_OK) {
                    failure("resampler_process", process_error);
                    ok = false;
                    break;
                }
                converted.resize(capacity);
                mono = std::move(converted);
            }
            had_audio = true;
            if (!mono.empty() && !sink(std::move(mono))) {
                failure("pcm_queue");
                ok = false;
                break;
            }
        }
        if (raw.consumed == 0 && frame.decoded_size == 0) {
            if (eof || buffered == input_size) {
                failure("decoder_no_progress");
                ok = false;
                break;
            }
        }
    }
    if (resampler)
        esp_ae_rate_cvt_close(resampler);
    esp_mp3_dec_close(decoder);
    ESP_LOGI(TAG, "End: ok=%d produced_pcm=%d cancelled=%d", ok, had_audio, cancelled());
    return ok && had_audio && !cancelled();
}
