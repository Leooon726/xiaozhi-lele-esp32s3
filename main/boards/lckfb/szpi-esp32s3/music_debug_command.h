#pragma once
#include <charconv>
#include <string_view>
#include "music_limits.h"

namespace TfMusic {
enum class DebugAction { Invalid, Play, Stop, Status, Verify };
struct DebugCommand {
    DebugAction action = DebugAction::Invalid;
    unsigned index = 0;
};
inline DebugCommand ParseDebugCommand(std::string_view line) {
    if (line == "music-test stop")
        return {DebugAction::Stop};
    if (line == "music-test status")
        return {DebugAction::Status};
    constexpr std::string_view play = "music-test play ";
    constexpr std::string_view verify = "music-test verify ";
    const auto action = line.starts_with(play) ? DebugAction::Play : DebugAction::Verify;
    const auto prefix = action == DebugAction::Play ? play : verify;
    if (!line.starts_with(prefix))
        return {};
    line.remove_prefix(prefix.size());
    unsigned index = 0;
    auto [end, error] = std::from_chars(line.data(), line.data() + line.size(), index);
    if (error != std::errc{} || end != line.data() + line.size() || index >= kMaxTracks)
        return {};
    return {action, index};
}
}  // namespace TfMusic
