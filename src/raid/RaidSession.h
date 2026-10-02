#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace noven::raid {
inline constexpr int kParserVersion = 1;
enum class GameMode { Unknown, PvP, PvE, Practice, Offline };
enum class RaidType { Unknown, PMC, Scav };
enum class RaidOutcome { Unknown, Survived, RunThrough, KIA, MIA, Left };
enum class SessionState { Idle, PreparingRaid, ActiveRaid, CompletedRaid };

// 时间为日志记录的本地墙钟毫秒，不伪装成 UTC；缺失值用 optional 表示。
// Timestamps are log-local wall-clock milliseconds, not UTC; absence is explicit.
struct RaidSession final {
    std::string localSessionId, eftRaidId, mapId;
    GameMode gameMode{GameMode::Unknown};
    RaidType raidType{RaidType::Unknown};
    RaidOutcome outcome{RaidOutcome::Unknown};
    std::optional<std::int64_t> startedAt, endedAt, duration;
    bool startObserved{}, endObserved{}, sourceInterrupted{};
    int parserVersion{kParserVersion};
    std::string startSource;
    std::uint64_t startOffset{};
};
}
