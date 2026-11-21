#pragma once
#include <lvgl.h>
#include "squirrel_expression.h"

inline constexpr uint16_t kSquirrelImageSize = 259;

const lv_image_dsc_t* GetSquirrelImage(SquirrelExpression expression);
