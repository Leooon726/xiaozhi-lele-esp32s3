"""Exercise production receive/drop accounting without changing queue policies."""
from pathlib import Path
import tempfile
import unittest
from test_startup_wake_word_hint import definition, ROOT
from test_wifi_upload import run_cpp


class AudioReceiveDiagnosticsTests(unittest.TestCase):
    def test_rejection_wait_cancellation_and_unlocked_snapshot(self):
        source = (ROOT / "main/audio/audio_service.cc").read_text()
        methods = "\n".join(definition(source, x) for x in (
            "bool AudioService::PushPacketToDecodeQueue(",
            "void AudioService::LogPlaybackDiagnostics("))
        code = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#define CONFIG_LICHUANG_TF_MUSIC_DEBUG 1
constexpr size_t MAX_DECODE_PACKETS_IN_QUEUE=2;
struct AudioStreamPacket {};
std::mutex* logged_mutex=nullptr;
std::array<unsigned long,8> values{};
void Capture(unsigned long seen,unsigned long dropped,unsigned long canceled,
             unsigned queue,unsigned pcm,unsigned high,bool decoding,bool outputting) {
    assert(logged_mutex->try_lock()); // Logging must never hold the audio queue lock.
    logged_mutex->unlock();
    values={seen,dropped,canceled,queue,pcm,high,unsigned(decoding),unsigned(outputting)};
}
#define ESP_LOGI(tag,format,...) Capture(__VA_ARGS__)
struct AudioService {
    std::mutex audio_queue_mutex_;
    std::condition_variable audio_queue_cv_;
    std::atomic<bool> service_stopped_{false};
    uint32_t playback_generation_=0;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_decode_queue_;
    std::deque<int> audio_playback_queue_;
    bool playback_drained_notified_=true,decode_in_flight_=false,output_in_flight_=false;
    uint32_t audio_diag_received_=0,audio_diag_queue_drops_=0,audio_diag_generation_drops_=0;
    size_t audio_diag_decode_high_water_=0;
    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket>,bool wait=false);
    void LogPlaybackDiagnostics();
};
METHODS
int main() {
    AudioService service;
    auto packet=[] {return std::make_unique<AudioStreamPacket>();};
    assert(service.PushPacketToDecodeQueue(packet()));
    assert(service.PushPacketToDecodeQueue(packet()));
    assert(!service.PushPacketToDecodeQueue(packet()));
    assert(service.audio_diag_received_==3 && service.audio_diag_queue_drops_==1);
    bool accepted=true;
    std::thread waiter([&] {accepted=service.PushPacketToDecodeQueue(packet(),true);});
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(service.audio_queue_mutex_);
            if(service.audio_diag_received_==4) {
                ++service.playback_generation_;
                service.audio_queue_cv_.notify_all();
                break;
            }
        }
        assert(std::chrono::steady_clock::now()<deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    waiter.join();
    assert(!accepted && service.audio_diag_generation_drops_==1);
    service.audio_decode_queue_.clear();
    assert(service.PushPacketToDecodeQueue(packet()));
    service.service_stopped_=true;
    assert(!service.PushPacketToDecodeQueue(packet()));
    service.audio_playback_queue_.push_back(1);
    service.decode_in_flight_=true;
    logged_mutex=&service.audio_queue_mutex_;
    service.LogPlaybackDiagnostics();
    assert((values==std::array<unsigned long,8>{6,1,2,1,1,2,1,0}));
}
'''.replace("METHODS", methods)
        with tempfile.TemporaryDirectory() as temporary:
            run_cpp(Path(temporary), code, extra=("-pthread",))
