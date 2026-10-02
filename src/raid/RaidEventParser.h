#pragma once
#include "raid/RaidSession.h"
#include <string_view>
#include <vector>

namespace noven::raid {
enum class EventKind { SessionModeDetected, MapPresetDetected, RaidIdentityDetected,
    RaidStarted, RaidEnded, ScavEvidenceDetected };
struct RaidEvent final {
    EventKind kind{};
    std::optional<std::int64_t> time;
    GameMode mode{GameMode::Unknown};
    std::string raidId, mapId;
    // 来源身份与完整行起始字节用于回放去重；绝不携带原始日志。
    // Source identity and complete-line byte offset support replay deduplication; no raw text.
    std::string source;
    std::uint64_t offset{};
};
[[nodiscard]] std::string NormalizeMap(std::string_view upstream);
[[nodiscard]] std::optional<std::int64_t> ParseLogTime(std::string_view line);
[[nodiscard]] std::vector<RaidEvent> ParseRaidEvents(std::string_view line,
    std::string_view source = {}, std::uint64_t offset = 0);
}
