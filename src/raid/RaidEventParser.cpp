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
    const auto first = value.find_first_not_of(" \t");
    if (first == value.npos) return {};
    value.remove_prefix(first);
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
    // 对照公开 maps 的 nameId/scenePath；只按精确身份关联既有 MapCatalog ID。
    // Verified against https://json.tarkov.dev/regular/maps (2026-10-05).
    // Like TarkovMonitor's public log contract, match nameId/scenePath exactly, never display-name substrings.
    struct Alias {std::string_view id, nameId, scenePath;};
    static constexpr Alias maps[]{
        {"55f2d3fd4bdc2d5f408b4567", "factory4_day", "maps/factory_day_preset.bundle"},
        {"56f40101d2720b2a4d8b45d6", "bigmap", "maps/customs_preset.bundle"},
        {"5704e3c2d2720bac5b8b4567", "Woods", "maps/woods_preset.bundle"},
        {"5704e4dad2720bb55b8b4567", "Lighthouse", "maps/lighthouse_preset.bundle"},
        {"5704e554d2720bac5b8b456e", "Shoreline", "maps/shoreline_preset.bundle"},
        {"5704e5fad2720bc05b8b4567", "RezervBase", "maps/rezerv_base_preset.bundle"},
        {"5714dbc024597771384a510d", "Interchange", "maps/shopping_mall.bundle"},
        {"5714dc692459777137212e12", "TarkovStreets", "maps/city_preset.bundle"},
        {"59fc81d786f774390775787e", "factory4_night", "maps/factory_night_preset.bundle"},
        {"5b0fc42d86f7744a585f9105", "laboratory", "maps/laboratory_preset.bundle"},
        {"653e6760052c01c1c805532f", "Sandbox", "maps/sandbox_preset.bundle"},
        {"65b8d6f5cdde2479cb2a3125", "Sandbox_high", "maps/sandbox_high_preset.bundle"},
        {"65cc8f81a9aac3e77d0cfd3e", "Terminal", "maps/terminal_preset.bundle"},
        {"6733700029c367a3d40b02af", "Labyrinth", "maps/labyrinth_preset.bundle"},
        {"68236e8153654e8c1200798a", "Sandbox_start", "maps/sandbox_start_preset.bundle"},
        {"69af492a4819ea4ba10a69c5", "Icebreaker", "maps/icebreaker.bundle"},
        {"6a294a5b5eb5f9a1700417b7", "laboratory_dark", "maps/laboratory_dark_preset.bundle"},
    };
    for (const auto& map : maps)
        if (upstream == map.nameId || upstream == map.scenePath) return std::string(map.id);
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
    const auto preset = Field(line, "scene preset path:");
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
