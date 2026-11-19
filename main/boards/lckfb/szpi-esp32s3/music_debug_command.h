#pragma once
#include <charconv>
#include <string_view>

namespace TfMusic {
enum class DebugAction { Invalid, Play, Stop, Status };
struct DebugCommand {
    DebugAction action = DebugAction::Invalid;
    unsigned index = 0;
};
inline DebugCommand ParseDebugCommand(std::string_view line) {
    if (line == "music-test stop")
        return {DebugAction::Stop};
    if (line == "music-test status")
        return {DebugAction::Status};
    constexpr std::string_view prefix = "music-test play ";
    if (!line.starts_with(prefix))
        return {};
    line.remove_prefix(prefix.size());
    unsigned index = 0;
    auto [end, error] = std::from_chars(line.data(), line.data() + line.size(), index);
    if (error != std::errc{} || end != line.data() + line.size() || index >= 128)
        return {};
    return {DebugAction::Play, index};
}
}  // namespace TfMusic
