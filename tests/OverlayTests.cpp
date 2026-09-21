#include "overlay/OverlayTypes.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    Require(noven::overlay::FormatRoubles(48900) == L"48,900 ₽",
        "rouble formatting groups thousands");
    Require(noven::overlay::FormatRoubles(1250000) == L"1,250,000 ₽",
        "rouble formatting handles millions");
    Require(noven::overlay::FormatOptionalRoubles(std::nullopt) == L"未知",
        "unknown price is not rendered as zero");

    Require(noven::overlay::FleaStatusDisplayName(noven::data::FleaStatus::Allowed)
                == std::wstring(L"可上架"),
        "allowed flea status is localized");
    Require(noven::overlay::FleaStatusDisplayName(noven::data::FleaStatus::Banned)
                == std::wstring(L"禁止上架"),
        "banned flea status is localized");
    Require(noven::overlay::FleaStatusDisplayName(noven::data::FleaStatus::Unknown)
                == std::wstring(L"未知"),
        "unknown flea status remains distinct");

    noven::data::ItemRecord localized;
    localized.nameZh = "金属零件";
    localized.nameEn = "Metal spare parts";
    Require(noven::overlay::DisplayNameForItem(localized) == "金属零件",
        "Chinese item name is preferred");
    localized.nameZh.clear();
    Require(noven::overlay::DisplayNameForItem(localized) == "Metal spare parts",
        "English item name is the fallback");

    Require(std::wstring(noven::data::UpstreamGameMode(noven::data::GameMode::Pvp))
                == L"regular",
        "PVP mode maps to regular");
    Require(std::wstring(noven::data::UpstreamGameMode(noven::data::GameMode::Pve))
                == L"pve",
        "PVE mode maps to pve");
    Require(std::wstring(noven::data::UpstreamGameMode(noven::data::GameMode::Seasonal))
                == L"pvp-season",
        "Seasonal mode maps to pvp-season");

    const auto upper_right = noven::overlay::CalculateOverlayPlacement(
        800, 500, 300, 150, 0, 0, 1920, 1080);
    Require(upper_right.left == 814 && upper_right.top == 336,
        "overlay prefers the upper-right of the cursor");

    const auto right_edge = noven::overlay::CalculateOverlayPlacement(
        1900, 500, 300, 150, 0, 0, 1920, 1080);
    Require(right_edge.left < 1900 && right_edge.top == 336,
        "overlay flips left near the right edge");

    const auto top_edge = noven::overlay::CalculateOverlayPlacement(
        800, 10, 300, 150, 0, 0, 1920, 1080);
    Require(top_edge.top > 10,
        "overlay flips below near the top edge");

    std::cout << "Overlay logic tests passed\n";
    return 0;
}
