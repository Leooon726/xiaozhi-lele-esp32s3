#pragma once
#include <string_view>

enum class SquirrelExpression {
    Neutral,
    Happy,
    Listening,
    Speaking,
    Thinking,
    Sad,
    Sleepy,
    Unsupported
};

inline SquirrelExpression SquirrelEmotion(std::string_view name) {
    if (name == "happy" || name == "laughing" || name == "funny" || name == "loving" ||
        name == "winking" || name == "kissy" || name == "delicious" || name == "confident" ||
        name == "cool" || name == "embarrassed")
        return SquirrelExpression::Happy;
    if (name == "sad" || name == "crying" || name == "angry")
        return SquirrelExpression::Sad;
    if (name == "thinking" || name == "surprised" || name == "shocked")
        return SquirrelExpression::Thinking;
    if (name == "sleepy" || name == "relaxed")
        return SquirrelExpression::Sleepy;
    if (name == "neutral" || name == "robot_2")
        return SquirrelExpression::Neutral;
    return SquirrelExpression::Unsupported;
}
