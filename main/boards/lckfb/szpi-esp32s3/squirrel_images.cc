#include "sdkconfig.h"
#if defined(CONFIG_LICHUANG_USB_STORAGE) && defined(CONFIG_USE_DEFAULT_MESSAGE_STYLE)
#include "squirrel_images.h"

namespace {
#define SQUIRREL_DATA(name)                                                          \
    extern const uint8_t name##_start[] asm("_binary_squirrel_" #name "_png_start"); \
    extern const uint8_t name##_end[] asm("_binary_squirrel_" #name "_png_end")
SQUIRREL_DATA(neutral);
SQUIRREL_DATA(happy);
SQUIRREL_DATA(listening);
SQUIRREL_DATA(speaking);
SQUIRREL_DATA(thinking);
SQUIRREL_DATA(sad);
SQUIRREL_DATA(sleepy);
#undef SQUIRREL_DATA

lv_image_dsc_t Image(const uint8_t* start, const uint8_t* end) {
    lv_image_dsc_t result{};
    result.header.magic = LV_IMAGE_HEADER_MAGIC;
    result.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    result.header.w = 144;
    result.header.h = 144;
    result.data_size = end - start;
    result.data = start;
    return result;
}
}  // namespace

const lv_image_dsc_t* GetSquirrelImage(SquirrelExpression expression) {
    static const lv_image_dsc_t images[] = {
        Image(neutral_start, neutral_end),     Image(happy_start, happy_end),
        Image(listening_start, listening_end), Image(speaking_start, speaking_end),
        Image(thinking_start, thinking_end),   Image(sad_start, sad_end),
        Image(sleepy_start, sleepy_end)};
    const auto index = static_cast<unsigned>(expression);
    return &images[index < sizeof(images) / sizeof(images[0]) ? index : 0];
}
#endif
