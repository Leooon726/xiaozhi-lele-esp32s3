"""Screen-only idle/wake coordination and rapid backlight reversal, no hardware."""
from pathlib import Path
import tempfile
import unittest

from test_wifi_upload import BOARD, run_cpp

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature, indent=4):
    start = source.index(signature)
    suffix = "\n" + " " * indent + "}"
    return source[start:source.index(suffix, start) + len(suffix)]


class ScreenIdleTests(unittest.TestCase):
    def compile(self, code):
        with tempfile.TemporaryDirectory() as temporary:
            run_cpp(Path(temporary), code)

    def test_timeout_activity_and_clock_reset(self):
        self.compile(r'''
#include <cassert>
#include "screen_idle.h"
int main(){
    LeleScreenIdle clock;
    assert(!clock.Due(100,false));
    assert(!clock.Due(60000099,false));
    assert(clock.Due(60000100,false));
    assert(!clock.Due(100000000,true));
    assert(!clock.Due(159999999,false));
    assert(clock.Due(160000000,false));
    clock.Activity(160000001);
    assert(!clock.Due(160000002,false));
    assert(!clock.Due(0,false));
    assert(clock.Due(60000000,false));
}
''')

    def test_panel_failures_wake_retry_input_race_and_brightness(self):
        source = (BOARD / "lele_display.h").read_text()
        methods = "\n".join(method(source, x) for x in
                            ("    void PollScreenIdle(", "    void SetPowerSaveMode("))
        self.compile(r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>
#include "screen_idle.h"
#define ESP_OK 0
#define ESP_LOGI(...)
#define ESP_LOGW(...)
int64_t now=0;
int64_t esp_timer_get_time(){return now;}
bool lock_ok=true,locked=false,panel_ok=true;
unsigned inactive=60000,panels=0,invalidated=0;
bool panel_on=true;
bool lvgl_port_lock(int){assert(!locked);locked=lock_ok;return locked;}
void lvgl_port_unlock(){assert(locked);locked=false;}
unsigned lv_display_get_inactive_time(void*){assert(locked);return inactive;}
int esp_lcd_panel_disp_on_off(void*,bool on){assert(locked);++panels;if(!panel_ok)return -1;panel_on=on;return 0;}
void* lv_display_get_screen_active(void* d){return d;}
void lv_obj_invalidate(void*){assert(locked);++invalidated;}
struct Backlight {std::vector<std::pair<int,bool>> calls;void SetBrightness(int value,bool permanent){calls.emplace_back(value,permanent);}};
int saved_brightness=42;
struct Settings {explicit Settings(const char*){}int GetInt(const char*,int){return saved_brightness;}};
struct Display {
    bool ready=false;void* display_=this;void* panel_=this;
    Backlight* backlight_=nullptr;
    LeleScreenIdle screen_idle_;
    std::atomic<bool> screen_off_{false};
    bool wake_requested_=false;int wake_brightness_=75;
    bool IsSetupUICalled(){return ready;}
METHODS
};
int main(){
    Display d;Backlight light;
    d.SetPowerSaveMode(false);assert(d.wake_requested_ && !panels);
    d.ready=true;d.backlight_=&light;d.PollScreenIdle(false);
    assert(!d.wake_requested_ && !panels);
    now=60000000;inactive=0;d.PollScreenIdle(false);
    assert(!d.screen_off_ && !panels); // A fresh input wins over the old sampled clock.
    inactive=60000;lock_ok=false;d.PollScreenIdle(false);assert(!panels);
    lock_ok=true;panel_ok=false;d.PollScreenIdle(false);
    assert(panels==1 && !d.screen_off_ && light.calls.empty());
    panel_ok=true;now+=100;d.PollScreenIdle(false);assert(panels==1);
    now+=60000000;d.PollScreenIdle(false);
    assert(d.screen_off_ && !panel_on && light.calls.back()==std::make_pair(0,false));
    const auto before=panels;d.SetPowerSaveMode(true);assert(panels==before);
    panel_ok=false;d.SetPowerSaveMode(false);
    assert(d.screen_off_ && d.wake_requested_ && light.calls.size()==1);
    panel_ok=true;d.PollScreenIdle(false);
    assert(!d.screen_off_ && !d.wake_requested_ && panel_on && invalidated==1);
    assert(light.calls.back()==std::make_pair(42,false));
    now+=59999999;d.PollScreenIdle(false);assert(!d.screen_off_);
    now+=1;d.PollScreenIdle(true);assert(!d.screen_off_);
    now+=60000000;d.PollScreenIdle(false);assert(d.screen_off_);
    lock_ok=false;d.SetPowerSaveMode(false);assert(d.screen_off_ && d.wake_requested_);
    lock_ok=true;d.PollScreenIdle(false);assert(!d.screen_off_ && panel_on);
    for(int value:{1,0,500}){
        saved_brightness=value;now+=60000000;d.PollScreenIdle(false);assert(d.screen_off_);
        d.SetPowerSaveMode(false);
        assert(light.calls.back()==std::make_pair(value<=0?10:std::min(value,100),false));
    }
    for(auto call:light.calls)assert(!call.second); // Temporary zero is never persisted.
}
'''.replace("METHODS", methods.replace(" override", "")))

    def test_first_touch_is_consumed_and_menu_gates(self):
        source = (BOARD / "lele_menu.cc").read_text()
        methods = "\n".join(method(source, x, 0) for x in
                            ("void LeleMenu::HandleAssistantTouch(",
                             "void LeleMenu::PollScreenIdle(", "bool LeleMenu::HandleClick("))
        methods = methods.replace("LeleMenu::", "")
        self.compile(r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include "assistant_tap.h"
#include <initializer_list>
#define CONFIG_USE_DEFAULT_MESSAGE_STYLE 1
#define ESP_LOGI(...)
enum {LV_EVENT_PRESSED,LV_EVENT_PRESSING,LV_EVENT_RELEASED,LV_EVENT_PRESS_LOST,LV_EVENT_CLICKED};
struct lv_event_t {int code;};
struct lv_point_t {int32_t x,y;};
struct Input{} input;
uint32_t tick=100;
int lv_event_get_code(lv_event_t* e){return e->code;}
Input* lv_indev_active(){return &input;}
void lv_indev_get_point(Input*,lv_point_t* p){*p={100,100};}
uint32_t lv_tick_get(){return tick;}
enum {kDeviceStateIdle,kDeviceStateListening};
struct Application {
    int state=kDeviceStateIdle;
    static Application& GetInstance(){static Application a;return a;}
    int GetDeviceState(){return state;}
};
struct Screen {
    bool off=true,held=false;int wakes=0;
    bool IsScreenOff(){return off;}
    void SetPowerSaveMode(bool on){assert(!on);off=false;++wakes;}
    void PollScreenIdle(bool keep){held=keep;}
};
struct LeleMenu {
    enum class Action {None,Return,Close};
    Action action_=Action::None;
    std::atomic<bool> screen_wake_pending_{false},assistant_tap_pending_{false};
    bool screen_wake_gesture_=false,open=false,transfer_boot_=false,preparing_=false;
    bool restart_to_transfer_=false,wifi_transfer_=false;
    AssistantTap assistant_tap_;
    Screen* orientation_display_=nullptr;
    bool IsOpen(){return open;}
METHODS
};
int main(){
    LeleMenu m;Screen s;m.orientation_display_=&s;
    auto event=[&](int code){lv_event_t e{code};m.HandleAssistantTouch(&e);};
    event(LV_EVENT_PRESSED);assert(m.screen_wake_pending_ && m.screen_wake_gesture_);
    assert(s.off && !m.assistant_tap_pending_); // Callback only queues hardware work.
    m.PollScreenIdle();assert(!s.off && s.wakes==1);
    tick+=80;event(LV_EVENT_RELEASED);
    assert(!m.assistant_tap_pending_ && !m.screen_wake_gesture_);
    tick+=400;event(LV_EVENT_PRESSED);tick+=80;event(LV_EVENT_RELEASED);
    assert(m.assistant_tap_pending_); // A separate second tap works normally.
    m.assistant_tap_pending_=false;s.off=true;
    event(LV_EVENT_PRESSED);m.PollScreenIdle();tick+=600;event(LV_EVENT_PRESS_LOST);
    assert(!m.assistant_tap_pending_ && !m.screen_wake_gesture_);
    s.off=true;assert(m.HandleClick() && m.screen_wake_pending_);
    m.PollScreenIdle();assert(!s.off && !m.HandleClick());
    for(bool* gate:{&m.open,&m.transfer_boot_,&m.preparing_,&m.restart_to_transfer_,&m.wifi_transfer_}) {
        *gate=true;m.PollScreenIdle();assert(s.held);*gate=false;
    }
    Application::GetInstance().state=kDeviceStateListening;m.PollScreenIdle();assert(s.held);
    Application::GetInstance().state=kDeviceStateIdle;m.PollScreenIdle();assert(!s.held);
}
'''.replace("METHODS", methods))

    def test_rapid_backlight_reversal_and_permanent_setting(self):
        source = (ROOT / "main/boards/common/backlight.cc").read_text()
        methods = "\n".join(method(source, x, 0) for x in
                            ("void Backlight::SetBrightness(", "void Backlight::OnTransitionTimer("))
        self.compile(r'''
#include <cassert>
#include <cstdint>
#define ESP_LOGI(...)
unsigned writes=0;int saved=-1;bool running=false;
struct Settings {Settings(const char*,bool){}void SetInt(const char*,int value){++writes;saved=value;}};
void esp_timer_start_periodic(void*,int){running=true;}
void esp_timer_stop(void*){running=false;}
struct Backlight {
    void* transition_timer_=this;uint8_t brightness_=0,target_brightness_=0,step_=1;
    int hardware=0;
    void SetBrightnessImpl(int value){hardware=value;}
    void SetBrightness(uint8_t,bool permanent=false);
    void OnTransitionTimer();
};
METHODS
int main(){
    Backlight b;b.SetBrightness(75);for(int i=0;i<75;++i)b.OnTransitionTimer();
    assert(b.hardware==75 && !running);
    b.SetBrightness(0);b.SetBrightness(75);b.OnTransitionTimer();
    assert(b.hardware==75 && b.brightness_==75 && !running && !writes);
    b.SetBrightness(0);for(int i=0;i<20;++i)b.OnTransitionTimer();assert(b.hardware==55);
    b.SetBrightness(75);for(int i=0;i<20;++i)b.OnTransitionTimer();assert(b.hardware==75 && !running);
    b.SetBrightness(75,true);assert(writes==1 && saved==75);
    b.SetBrightness(255,true);assert(saved==100);for(int i=0;i<25;++i)b.OnTransitionTimer();
    assert(b.hardware==100);
    b.SetBrightness(0);for(int i=0;i<100;++i)b.OnTransitionTimer();
    assert(!running && b.hardware==0 && saved==100);
}
'''.replace("METHODS", methods))

    def test_wake_word_wakes_before_protocol_check(self):
        source = (ROOT / "main/application.cc").read_text()
        start = source.index("void Application::HandleWakeWordDetectedEvent() {")
        end = source.index("    auto state = GetDeviceState();", start)
        prefix = source[start:end] + "}\n"
        self.compile(r'''
#include <cassert>
#define CONFIG_LICHUANG_USB_STORAGE 1
#define CONFIG_USE_DEFAULT_MESSAGE_STYLE 1
struct Display {int wakes=0;void SetPowerSaveMode(bool on){assert(!on);++wakes;}};
struct Board {
    Display display;
    static Board& GetInstance(){static Board board;return board;}
    Display* GetDisplay(){return &display;}
};
struct Application {void* protocol_=nullptr;void HandleWakeWordDetectedEvent();};
METHOD
int main(){
    Application app;app.HandleWakeWordDetectedEvent();
    assert(Board::GetInstance().display.wakes==1); // No network/protocol needed to light the screen.
    app.protocol_=&app;app.HandleWakeWordDetectedEvent();
    assert(Board::GetInstance().display.wakes==2);
}
'''.replace("METHOD", prefix))
