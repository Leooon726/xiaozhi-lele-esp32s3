"""Exercise the board's actual state/emotion methods without a physical panel."""
from pathlib import Path
import tempfile
import unittest

from test_wifi_upload import BOARD, run_cpp


class SquirrelDisplayTests(unittest.TestCase):
    def test_activity_emotions_and_renderer_failures(self):
        source = (BOARD / "lele_display.h").read_text()
        methods = []
        for signature in ("    void SetStatus(", "    bool SetEmotion(", "    bool ShowSquirrel("):
            start = source.index(signature)
            end = source.index("\n    }", start) + len("\n    }")
            methods.append(source[start:end])
        code = r'''
#include <atomic>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include "squirrel_expression.h"
bool lock_ok=true,locked=false;
unsigned stopped=0,draws=0;
unsigned scale=0;
bool overflow_visible=false;
constexpr int LV_OBJ_FLAG_OVERFLOW_VISIBLE=1;
void lv_image_set_scale(void*,unsigned value){assert(locked);scale=value;}
void lv_obj_add_flag(void*,int flag){assert(locked && flag==LV_OBJ_FLAG_OVERFLOW_VISIBLE);overflow_visible=true;}
SquirrelExpression selected=SquirrelExpression::Unsupported;
const SquirrelExpression* current_image=nullptr;
bool lvgl_port_lock(int){assert(!locked);locked=lock_ok;return locked;}
void lvgl_port_unlock(){assert(locked);locked=false;}
const SquirrelExpression* GetSquirrelImage(SquirrelExpression value){
    static const SquirrelExpression images[]={SquirrelExpression::Neutral,SquirrelExpression::Happy,
        SquirrelExpression::Listening,SquirrelExpression::Speaking,SquirrelExpression::Thinking,
        SquirrelExpression::Sad,SquirrelExpression::Sleepy};
    return &images[unsigned(value)];
}
const void* lv_image_get_src(void*){return current_image;}
void lv_image_set_src(void*,const SquirrelExpression* image){
    assert(locked);current_image=image;selected=*image;++draws;
}
namespace Lang::Strings {
constexpr auto LISTENING="Listening",SPEAKING="Speaking",CONNECTING="Connecting";
}
struct Gif {void Stop(){assert(locked);++stopped;}};
struct SpiLcdDisplay {
    bool setup=false,shown=false,emotion_uses_image_=false;
    void* emoji_image_=this;
    void* emoji_box_=this;
    std::unique_ptr<Gif> gif_controller_;
    std::string status;
    virtual void SetStatus(const char* value){status=value;}
    virtual bool SetEmotion(const char*){return false;}
    bool IsSetupUICalled(){return setup;}
    void ShowEmotion(){assert(locked);shown=emotion_uses_image_;}
};
struct TestDisplay : SpiLcdDisplay {
    std::atomic<SquirrelExpression> activity_{SquirrelExpression::Neutral};
METHODS
};
int main(){
    TestDisplay d;
    assert(!d.SetEmotion("neutral") && !draws);
    d.setup=true;
    assert(d.SetEmotion("robot_2") && selected==SquirrelExpression::Neutral && d.shown);
    assert(scale==461 && overflow_visible);
    const auto neutral_draws=draws;
    assert(d.SetEmotion("neutral") && draws==neutral_draws);
    d.SetStatus(Lang::Strings::LISTENING);
    assert(d.status=="Listening" && selected==SquirrelExpression::Listening);
    assert(d.SetEmotion("neutral") && selected==SquirrelExpression::Listening);
    d.SetStatus(Lang::Strings::CONNECTING);
    assert(selected==SquirrelExpression::Thinking);
    d.SetStatus(Lang::Strings::SPEAKING);
    assert(d.SetEmotion("neutral") && selected==SquirrelExpression::Speaking);
    assert(d.SetEmotion("happy") && selected==SquirrelExpression::Happy);
    for(const char* name:{"laughing","funny","loving","winking","kissy","cool","delicious",
                         "confident","embarrassed"})
        assert(d.SetEmotion(name) && selected==SquirrelExpression::Happy);
    for(const char* name:{"sad","angry","crying"})
        assert(d.SetEmotion(name) && selected==SquirrelExpression::Sad);
    for(const char* name:{"thinking","shocked","surprised"})
        assert(d.SetEmotion(name) && selected==SquirrelExpression::Thinking);
    for(const char* name:{"sleepy","relaxed"})
        assert(d.SetEmotion(name) && selected==SquirrelExpression::Sleepy);
    d.SetStatus("Standby");
    assert(selected==SquirrelExpression::Neutral);
    const auto before=draws;
    assert(!d.SetEmotion(nullptr) && !d.SetEmotion("") && !d.SetEmotion("missing"));
    d.SetStatus(nullptr);
    assert(draws==before && !locked);
    lock_ok=false;
    assert(!d.SetEmotion("happy") && draws==before && !locked);
    lock_ok=true;
    d.gif_controller_=std::make_unique<Gif>();
    assert(d.SetEmotion("neutral") && stopped==1 && !d.gif_controller_);
    d.emoji_image_=nullptr;
    assert(!d.SetEmotion("happy"));
}
'''.replace("METHODS", "\n".join(methods))
        with tempfile.TemporaryDirectory() as temporary:
            run_cpp(Path(temporary), code)


if __name__ == "__main__":
    unittest.main()
