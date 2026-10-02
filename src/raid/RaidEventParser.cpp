#include "raid/RaidEventParser.h"
#include <charconv>
#include <chrono>

namespace noven::raid {
namespace {
bool Identity(std::string_view value) {
    return !value.empty() && value.size() <= 128
        && value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == value.npos;
}
std::string_view Field(std::string_view line, std::string_view key) {
    const auto pos = line.find(key);
    if (pos == line.npos) return {};
    auto value = line.substr(pos + key.size());
    return value.substr(0, value.find_first_of(" ,\t\r\n|"));
}
bool Token(std::string_view line, std::string_view token) {
    const auto pos = line.find(token);
    if (pos == line.npos) return false;
    const auto word = [](char c) { return (c >= 'a' && c <= 'z')
        || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/'; };
    bool leftBoundary = pos == 0 || !word(line[pos - 1]);
    if (!leftBoundary && token.starts_with('/')) {
        // backend 请求可带完整 URL；仅确认其路径边界，不提取或保存主机。
        // Backend requests may include a full URL; validate the path boundary, never extract/store the host.
        const auto scheme = line.rfind("://", pos);
        leftBoundary = scheme != line.npos && line.find('/', scheme + 3) == pos
            && line.substr(scheme + 3, pos - scheme - 3).find_first_of(" \t\r\n|\"") == line.npos;
    }
    return leftBoundary
        && (pos + token.size() == line.size() || !word(line[pos + token.size()]));
}
}
std::string NormalizeMap(std::string_view upstream) {
    // 仅纳入已核对的精确别名；使用 MapCatalog 的稳定 ID，不猜测未知地图。
    // Only verified exact aliases map to MapCatalog stable IDs; never guess unknown maps.
    if (upstream == "RezervBase" || upstream == "maps/rezerv_base_preset.bundle")
        return "5704e5fad2720bc05b8b4567";
    return {};
}
std::optional<std::int64_t> ParseLogTime(std::string_view line) {
    if (line.size() < 23 || line[4] != '-' || line[7] != '-' || line[10] != ' '
        || line[13] != ':' || line[16] != ':' || line[19] != '.') return {};
    const auto number = [&](std::size_t pos, std::size_t length) {
        int value = -1;
        const auto text = line.substr(pos, length);
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        return ec == std::errc{} && end == text.data() + text.size() ? value : -1;
    };
    const int y = number(0,4), m = number(5,2), d = number(8,2);
    const int h = number(11,2), minute = number(14,2), s = number(17,2), ms = number(20,3);
    if (y < 2000 || y > 9999 || m < 1 || d < 1 || h < 0 || h > 23
        || minute < 0 || minute > 59 || s < 0 || s > 59 || ms < 0) return {};
    const std::chrono::year_month_day date{std::chrono::year(y),
        std::chrono::month(static_cast<unsigned>(m)), std::chrono::day(static_cast<unsigned>(d))};
    if (!date.ok()) return {};
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::sys_days(date).time_since_epoch()).count()
        + ((h * 60LL + minute) * 60 + s) * 1000 + ms;
}
std::vector<RaidEvent> ParseRaidEvents(std::string_view line, std::string_view source,
    std::uint64_t offset) {
    std::vector<RaidEvent> events;
    if (line.size() > 65536 || line.find('\0') != line.npos) return events;
    const auto add = [&](EventKind kind) -> RaidEvent& {
        RaidEvent event; event.kind = kind; event.time = ParseLogTime(line);
        event.source = source; event.offset = offset;
        events.push_back(std::move(event)); return events.back();
    };
    const auto mode = Field(line, "Session mode: ");
    if (mode == "Pve" || mode == "Pvp")
        add(EventKind::SessionModeDetected).mode = mode == "Pve" ? GameMode::PvE : GameMode::PvP;
    const auto preset = Field(line, "scene preset path: ");
    if (!preset.empty()) add(EventKind::MapPresetDetected).mapId = NormalizeMap(preset);
    if (line.find("[Transit]") != line.npos) {
        const auto id = Field(line, "RaidId:");
        if (Identity(id)) {
            auto& event = add(EventKind::RaidIdentityDetected);
            event.raidId = id; event.mapId = NormalizeMap(Field(line, "Locations:"));
        }
    }
    if (Token(line,"GameStarted")) add(EventKind::RaidStarted);
    if (Token(line,"/client/match/local/end")) add(EventKind::RaidEnded);
    if (Token(line,"SellAsSavage") || Token(line,"FinishScavSession")
        || Token(line,"/client/game/profile/savage/regenerate")) add(EventKind::ScavEvidenceDetected);
    return events;
}
}
