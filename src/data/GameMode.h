#pragma once

#include <array>

namespace noven::data {

enum class GameMode {
    Pvp,
    Pve,
    Seasonal,
};

[[nodiscard]] constexpr const wchar_t* GameModeName(GameMode mode) noexcept {
    switch (mode) {
    case GameMode::Pvp:
        return L"PVP";
    case GameMode::Pve:
        return L"PVE";
    case GameMode::Seasonal:
        return L"SEASONAL";
    }
    return L"UNKNOWN";
}

[[nodiscard]] constexpr const wchar_t* UpstreamGameMode(GameMode mode) noexcept {
    switch (mode) {
    case GameMode::Pvp:
        return L"regular";
    case GameMode::Pve:
        return L"pve";
    case GameMode::Seasonal:
        return L"pvp-season";
    }
    return L"regular";
}

[[nodiscard]] constexpr const char* UpstreamGameModeCode(GameMode mode) noexcept {
    switch (mode) {
    case GameMode::Pvp:
        return "regular";
    case GameMode::Pve:
        return "pve";
    case GameMode::Seasonal:
        return "pvp-season";
    }
    return "regular";
}

[[nodiscard]] constexpr std::array<GameMode, 3> AllGameModes() noexcept {
    return {GameMode::Pvp, GameMode::Pve, GameMode::Seasonal};
}

} // namespace noven::data
