#include "data/ItemCatalog.h"
#include "scanner/SpatialCandidateSelector.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

noven::scanner::MatchedText MakeMatched(
    const noven::data::ItemCatalog& catalog,
    const std::string& text,
    float center_x,
    float center_y,
    float confidence = 0.9F
) {
    const auto matches = catalog.Match(text);
    Require(!matches.empty(), "test text has a catalog match");
    return noven::scanner::MatchedText{
        noven::ocr::RecognizedText{
            noven::ocr::TextBox{center_x - 40.0F, center_y - 12.0F,
                                center_x + 40.0F, center_y + 12.0F, confidence},
            text,
            confidence,
        },
        matches.front(),
        {},
        1.0F,
        false,
    };
}

noven::scanner::MatchedText MakeMatchedBox(
    const noven::data::ItemCatalog& catalog,
    const std::string& text,
    float x1,
    float y1,
    float x2,
    float y2,
    float confidence = 0.9F
) {
    auto matched = MakeMatched(
        catalog,
        text,
        (x1 + x2) / 2.0F,
        (y1 + y2) / 2.0F,
        confidence
    );
    matched.recognized.box = noven::ocr::TextBox{x1, y1, x2, y2, confidence};
    return matched;
}

const noven::scanner::ScanCandidate& Selected(
    const noven::scanner::ScanResult& result
) {
    Require(result.found && result.selected.has_value(), "a spatial candidate is selected");
    return *result.selected;
}

noven::scanner::MatchedText MakeAmbiguousMatched(
    const noven::data::ItemCatalog& catalog,
    const std::string& text,
    float center_x,
    float center_y
) {
    auto matched = MakeMatched(catalog, text, center_x, center_y);
    matched.match.ambiguous = true;
    matched.match.competitiveCandidateCount = 2;
    matched.match.scoreGap = 0.0F;
    return matched;
}

} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path()
        / "noven_spatial_candidate_catalog.tsv";
    {
        std::ofstream file(path, std::ios::binary);
        Require(static_cast<bool>(file), "spatial test catalog can be created");
        file << "# test catalog\n"
             << "id\tnameZh\tshortNameZh\tnameEn\tshortNameEn\twidth\theight\n"
             << "61bf7b6302b3924be92fa8c3\t金属零件\t金属零件\tMetal spare parts\tM.parts\t1\t1\n"
             << "5df8a2ca86f7740bfe6df777\t6B2 护甲\t6B2\t6B2 body armor (Flora)\t6B2\t2\t1\n"
             << "5447a9cd4bdc2dbd208b4567\t工具包\t小包\tSmall bag\tBag\t1\t1\n";
    }

    noven::data::ItemCatalog catalog;
    std::wstring error;
    Require(catalog.Load(path, error), "spatial test catalog loads");
    noven::scanner::SpatialCandidateSelector selector;
    const noven::scanner::AnchorPoint anchor{400.0F, 300.0F};

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatchedBox(catalog, "Metal spare parts", 408.0F, 40.0F, 808.0F, 300.0F),
            MakeMatchedBox(catalog, "6B2 body armor (Flora)", 450.0F, 220.0F, 530.0F, 244.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "61bf7b6302b3924be92fa8c3",
            "rectangle edge distance beats center distance");
        Require(Selected(result).distanceToAnchor == 8.0F,
            "selected candidate reports cursor-to-rectangle distance");
        Require(Selected(result).centerDistanceToAnchor > 150.0F,
            "center distance remains an independent diagnostic");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatchedBox(catalog, "Metal spare parts", 500.0F, 180.0F, 580.0F, 204.0F),
            MakeMatchedBox(catalog, "6B2 body armor (Flora)", 430.0F, 220.0F, 520.0F, 250.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory,
            anchor,
            texts,
            noven::ocr::TextBox{408.0F, 190.0F, 560.0F, 280.0F, 1.0F}
        );
        Require(Selected(result).match.item->id == "5df8a2ca86f7740bfe6df777",
            "tooltip-inside candidate beats unrelated outside text");
        Require(Selected(result).insideTooltip,
            "selected candidate is marked inside tooltip");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeAmbiguousMatched(catalog, "Metal spare parts", 420.0F, 260.0F),
            MakeMatchedBox(catalog, "6B2 body armor (Flora)", 500.0F, 228.0F, 580.0F, 252.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "5df8a2ca86f7740bfe6df777",
            "ambiguous nearest candidate does not stop the scan");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "金属零件", 500.0F, 220.0F),
            MakeMatched(catalog, "6B2 body armor (Flora)", 560.0F, 260.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).direction == noven::scanner::ScanDirection::UpperRight,
            "upper-right beats right");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "金属零件", 560.0F, 220.0F),
            MakeMatched(catalog, "6B2 body armor (Flora)", 500.0F, 250.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "5df8a2ca86f7740bfe6df777",
            "near right candidate beats farther upper-right candidate");
        Require(result.nearestValid.has_value()
                && result.nearestValid->match.item->id == "5df8a2ca86f7740bfe6df777",
            "nearest valid baseline identifies the nearby candidate");
        Require(result.selected->participatingInSearch,
            "selected candidate belongs to the winning local pool");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "6B2 body armor (Flora)", 400.0F, 180.0F),
            MakeMatched(catalog, "金属零件", 760.0F, 80.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "5df8a2ca86f7740bfe6df777",
            "a Ring 0 local candidate prevents a Ring 2 candidate competing");
        Require(result.searchSteps.size() == 3
                && result.searchSteps.front().direction
                    == noven::scanner::ScanDirection::UpperRight
                && result.searchSteps[1].direction
                    == noven::scanner::ScanDirection::Up
                && result.searchSteps[1].action
                    == noven::scanner::SpatialSearchAction::Select,
            "nearest candidate is selected inside the upper-half search order");
        Require(!result.considered[1].participatingInSearch,
            "distant unrelated candidate is outside the winning local pool");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeAmbiguousMatched(catalog, "金属零件", 500.0F, 220.0F),
            MakeMatched(catalog, "6B2 body armor (Flora)", 400.0F, 180.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "5df8a2ca86f7740bfe6df777",
            "ambiguous upper-right candidate does not stop local search");
        Require(result.searchSteps.front().ambiguousCandidateCount == 1
                && result.searchSteps.front().acceptedCandidateCount == 0,
            "ambiguous local candidate is reported but not accepted");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeAmbiguousMatched(catalog, "金属零件", 500.0F, 220.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(!result.found && !result.selected.has_value(),
            "an ambiguous-only local scan returns no result");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "金属零件", 520.0F, 300.0F),
            MakeMatched(catalog, "6B2 body armor (Flora)", 500.0F, 240.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "5df8a2ca86f7740bfe6df777",
            "preferred UpperRight sector is searched before Right");
    }

    {
        auto nearby = MakeMatched(catalog, "金属零件", 460.0F, 260.0F);
        nearby.match.score = 0.82F;
        nearby.evidenceCoverage = 0.95F;
        auto distant = MakeMatched(catalog, "6B2 body armor (Flora)", 680.0F, 160.0F);
        distant.match.score = 0.99F;
        distant.evidenceCoverage = 0.40F;
        const std::vector<noven::scanner::MatchedText> texts{nearby, distant};
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "61bf7b6302b3924be92fa8c3",
            "complete nearby evidence beats a distant partial match");
    }

    {
        auto short_evidence = MakeMatched(
            catalog, "Metal spare parts", 450.0F, 250.0F, 0.99F);
        short_evidence.match.score = 0.99F;
        short_evidence.evidenceCoverage = 0.20F;
        auto complete_evidence = MakeMatched(
            catalog, "6B2 body armor (Flora)", 520.0F, 220.0F, 0.90F);
        complete_evidence.match.score = 0.95F;
        complete_evidence.evidenceCoverage = 0.95F;
        const std::vector<noven::scanner::MatchedText> texts{
            short_evidence,
            complete_evidence,
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "5df8a2ca86f7740bfe6df777",
            "high coverage beats a low-coverage high-similarity substring");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "金属零件", 470.0F, 230.0F),
            MakeMatched(catalog, "6B2 body armor (Flora)", 500.0F, 200.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "61bf7b6302b3924be92fa8c3",
            "nearer same-direction candidate wins");
    }

    {
        auto grouped = MakeMatched(catalog, "6B2 body armor (Flora)", 500.0F, 220.0F);
        grouped.grouped = true;
        grouped.sourceBoxIndices = {3, 4};
        const std::vector<noven::scanner::MatchedText> texts{grouped};
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).grouped,
            "a locally grouped item name remains selectable");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "金属零件", 650.0F, 180.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).distanceToAnchor > 160.0F,
            "search expands to a later ring");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "金属零件", 300.0F, 300.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(!result.found,
            "directions outside the upper-half inventory policy are not scanned");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            noven::scanner::MatchedText{
                noven::ocr::RecognizedText{
                    noven::ocr::TextBox{460.0F, 200.0F, 540.0F, 224.0F, 0.9F},
                    "not a Tarkov item",
                    0.9F,
                },
                noven::data::ItemMatch{},
                {},
                1.0F,
                false,
            },
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(!result.found, "invalid catalog text is skipped");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "6B2 bodi armor (Fora)", 500.0F, 220.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "5df8a2ca86f7740bfe6df777",
            "English OCR typo uses catalog fuzzy matching");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "小包", 500.0F, 220.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(Selected(result).match.item->id == "5447a9cd4bdc2dbd208b4567",
            "a short Chinese item name remains valid");
        Require(Selected(result).match.matchType == noven::data::MatchType::ExactShortName,
            "Chinese short name uses its short-name alias");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "金属零件", 400.0F, 380.0F),
            MakeMatched(catalog, "6B2 body armor (Flora)", 400.0F, 220.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::RaidPickup, anchor, texts);
        Require(Selected(result).direction == noven::scanner::ScanDirection::Down,
            "raid pickup prefers below the crosshair");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "金属零件", 500.0F, 220.0F),
            MakeMatched(catalog, "6B2 body armor (Flora)", 520.0F, 260.0F),
        };
        const auto result = selector.Select(
            noven::scanner::ScanProfileType::Inventory, anchor, texts);
        Require(result.considered.size() == 2,
            "recognized boxes remain independent");
        Require(Selected(result).recognized.text != "金属零件 6B2 body armor (Flora)",
            "recognized boxes are never concatenated");
    }

    {
        const std::vector<noven::scanner::MatchedText> texts{
            MakeMatched(catalog, "金属零件", 500.0F, 220.0F),
        };
        const auto start = std::chrono::steady_clock::now();
        for (int iteration = 0; iteration < 5000; ++iteration) {
            static_cast<void>(selector.Select(
                noven::scanner::ScanProfileType::Inventory, anchor, texts));
        }
        const double elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "Spatial selection average ms=" << elapsed_ms / 5000.0 << '\n';
    }

    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    Require(!remove_error, "spatial test catalog is removed");
    std::cout << "Spatial candidate selector tests passed\n";
    return 0;
}
