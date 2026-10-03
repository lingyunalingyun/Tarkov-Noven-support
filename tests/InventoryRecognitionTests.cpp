#include "scanner/InventoryRecognition.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

noven::ocr::RecognizedText Text(std::string value) {
    return {{10.0F, 10.0F, 230.0F, 32.0F, 0.98F}, std::move(value), 0.98F};
}

noven::scanner::InventoryRecognitionResult Resolve(
    const noven::data::ItemCatalog& catalog,
    std::string value,
    std::uint64_t scan_id = 184
) {
    auto result = noven::scanner::BuildInventoryRecognition(
        scan_id,
        noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt,
        {Text(std::move(value))}
    );
    noven::scanner::ResolveInventoryCatalog(result, catalog, 0.64F);
    Require(result.catalogAttempted, "nonempty OCR always enters catalog resolver");
    Require(result.scanId == scan_id, "scan ID survives OCR and catalog resolution");
    return result;
}

} // namespace

int main(int argc, char** argv) {
    Require(argc == 2, "catalog path supplied");
    noven::data::ItemCatalog catalog;
    std::wstring error;
    Require(catalog.Load(std::filesystem::path(argv[1]), error), "catalog loads");
    const auto chinese = Resolve(catalog, "金属零件");
    Require(chinese.selectedItemId == "61bf7b6302b3924be92fa8c3",
        "exact Chinese title resolves stable item ID");
    const auto english = Resolve(catalog, "Metal spare parts");
    Require(english.selectedItemId == chinese.selectedItemId,
        "English alias resolves same stable item ID");

    const auto mixed = Resolve(catalog, "Camelbak Tri-Zip 突击背包 (叶绿色)");
    Require(mixed.selectedItemId == "545cdae64bdc2d39198b4568",
        "mixed Chinese and English punctuation resolves");
    const auto punctuation = Resolve(catalog, "Camelbak Tri-Zip 突击背包（叶绿色）");
    Require(punctuation.selectedItemId == mixed.selectedItemId,
        "full-width punctuation resolves same item");

    const auto mdr_short = Resolve(catalog, "MDR");
    Require(mdr_short.selectedItemId.empty(), "short MDR remains ambiguous");
    Require(mdr_short.status == noven::scanner::InventoryOutcomeStatus::CatalogAmbiguous,
        "ambiguous OCR has explicit outcome");
    const auto mdr_full = Resolve(catalog, "Desert Tech MDR 5.56x45 assault rifle");
    Require(mdr_full.selectedItemId == "5c488a752e221602b412af63",
        "full MDR title selects rifle");
    const auto mdr_missing_first = Resolve(catalog, "esert Tech MDR 5.56x45 assault rifle");
    Require(mdr_missing_first.selectedItemId == mdr_full.selectedItemId,
        "missing first OCR character still retrieves rifle");

    auto split_title = noven::scanner::BuildInventoryRecognition(
        186,
        noven::scanner::InventoryRecognitionPath::AdaptiveFallback,
        std::nullopt,
        {
            {{10.0F, 10.0F, 100.0F, 32.0F, 0.96F}, "Desert Tech MDR", 0.96F},
            {{104.0F, 10.0F, 240.0F, 32.0F, 0.94F},
             "5.56x45 assault rifle", 0.94F},
        }
    );
    noven::scanner::ResolveInventoryCatalog(split_title, catalog, 0.64F);
    Require(split_title.catalogAttempted && split_title.scanId == 186,
        "grouped OCR retains scan correlation and always resolves");
    Require(split_title.selectedItemId == mdr_full.selectedItemId,
        "complete split title beats partial fragment");
    Require(split_title.selectedCandidate.has_value()
        && split_title.candidates[*split_title.selectedCandidate].stage == "full",
        "complete title is preferred over isolated fragment");

    const auto no_match = Resolve(catalog, "not a Tarkov item name");
    Require(no_match.selectedItemId.empty() && !no_match.rejectionReason.empty(),
        "failed catalog resolution has explicit reason");
    auto empty = noven::scanner::BuildInventoryRecognition(
        185,
        noven::scanner::InventoryRecognitionPath::AdaptiveFallback,
        std::nullopt,
        {}
    );
    noven::scanner::ResolveInventoryCatalog(empty, catalog, 0.64F);
    Require(empty.catalogAttempted && empty.status == noven::scanner::InventoryOutcomeStatus::OcrEmpty,
        "empty OCR also produces explicit catalog outcome");

    noven::data::ItemEconomyStore empty_store;
    const auto missing_economy = noven::scanner::ResolveInventoryEconomy(
        chinese, empty_store, noven::data::GameMode::Pvp);
    Require(!missing_economy.found && missing_economy.itemId == chinese.selectedItemId,
        "economy lookup uses stable item ID");
    const auto display = noven::scanner::BuildInventoryDisplayResult(
        chinese, missing_economy, noven::data::GameMode::Pvp);
    Require(display.has_value() && display->itemId == chinese.selectedItemId
            && display->displayName == "金属零件",
        "catalog selection builds display even when economy is missing");
    Require(display->matchQuality == noven::overlay::MatchQuality::Strict
            && display->rawOcrText == "金属零件"
            && display->assembledOcrText == "金属零件",
        "strict display retains raw and assembled OCR evidence");
    Require(!display->fleaPrice.has_value(), "missing price stays unknown");
    const auto& resolved_text = chinese.candidates[*chinese.selectedCandidate].text;
    const noven::scanner::SpatialCandidateSelector legacy_selector;
    const auto legacy_result = legacy_selector.Select(
        noven::scanner::ScanProfileType::Inventory,
        {2000.0F, 2000.0F},
        std::span<const noven::scanner::MatchedText>(&resolved_text, 1)
    );
    Require(!legacy_result.selected.has_value(),
        "legacy spatial gate rejects a distant catalog-valid result");
    Require(display.has_value(),
        "inventory display is no longer gated by a second spatial selection");
    Require(!noven::scanner::BuildInventoryDisplayResult(
        no_match, {}, noven::data::GameMode::Pvp).has_value(),
        "catalog failure does not create a fake item card");
    auto lowest_confidence = no_match;
    noven::scanner::BestEffortResolve(lowest_confidence, catalog);
    const auto lowest_confidence_display = noven::scanner::BuildInventoryDisplayResult(
        lowest_confidence, {}, noven::data::GameMode::Pvp);
    Require(lowest_confidence.matchMode == noven::scanner::InventoryMatchMode::BestEffort
            && lowest_confidence_display.has_value()
            && !lowest_confidence_display->itemId.empty()
            && lowest_confidence_display->displayName != lowest_confidence.assembledText,
        "healthy catalog converts even a weak OCR query into a canonical low-confidence result");

    auto strict_unchanged = chinese;
    noven::scanner::BestEffortResolve(strict_unchanged, catalog);
    Require(strict_unchanged.matchMode == noven::scanner::InventoryMatchMode::Strict
            && strict_unchanged.selectedItemId == chinese.selectedItemId,
        "strict match wins without entering best-effort");

    auto mdr_fallback = mdr_short;
    noven::scanner::BestEffortResolve(mdr_fallback, catalog);
    Require(mdr_fallback.matchMode == noven::scanner::InventoryMatchMode::BestEffort
            && !mdr_fallback.selectedItemId.empty(),
        "strict-ambiguous MDR still produces best-effort item");
    Require(mdr_fallback.bestEffortTop.size() == 3 && mdr_fallback.bestEffortAmbiguous,
        "near-tied MDR candidates are reported as ambiguous with top three");
    const auto fallback_economy = noven::scanner::ResolveInventoryEconomy(
        mdr_fallback, empty_store, noven::data::GameMode::Pvp);
    Require(fallback_economy.itemId == mdr_fallback.selectedItemId,
        "best-effort passes stable item ID into economy lookup");
    noven::data::ItemEconomyStore populated_store;
    // 此夹具只有跳蚤价；商人报价缺失不应阻断稳定 ID 的展示。
    // This fixture has only a flea price; missing trader offers must not
    // prevent display of the stable catalog item.
    const std::string economy_payload = std::string(
        "{\"data\":{\"fleaMarket\":{\"enabled\":true},\"items\":{\"")
        + mdr_fallback.selectedItemId + "\":{\"id\":\""
        + mdr_fallback.selectedItemId
        + "\",\"width\":1,\"height\":1,\"lastLowPrice\":12345,"
          "\"types\":[],\"sellFor\":[]}}}}";
    Require(populated_store.ReplaceFromUpstreamJson(
        noven::data::GameMode::Pvp, economy_payload, error),
        "offline economy fixture loads");
    const auto populated_economy = noven::scanner::ResolveInventoryEconomy(
        mdr_fallback, populated_store, noven::data::GameMode::Pvp);
    const auto populated_display = noven::scanner::BuildInventoryDisplayResult(
        mdr_fallback, populated_economy, noven::data::GameMode::Pvp);
    Require(populated_economy.found && populated_display.has_value()
            && populated_display->fleaPrice == 12345,
        "best-effort item resolves real economy data by stable ID");
    const auto fallback_display = noven::scanner::BuildInventoryDisplayResult(
        mdr_fallback, fallback_economy, noven::data::GameMode::Pvp);
    Require(fallback_display.has_value()
            && fallback_display->matchQuality == noven::overlay::MatchQuality::LowConfidence
            && fallback_display->bestEffortAmbiguous
            && fallback_display->rawOcrText == "MDR"
            && !fallback_display->fleaPrice.has_value(),
        "missing economy still shows low-confidence item card");

    auto noisy = noven::scanner::BuildInventoryRecognition(
        189, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("CamolBak Tri Zp 突击背包 叶绿")}
    );
    noven::scanner::ResolveInventoryCatalog(noisy, catalog, 1.01F);
    Require(noisy.selectedItemId.empty(), "strict threshold rejects noisy long title");
    noven::scanner::BestEffortResolve(noisy, catalog);
    Require(noisy.matchMode == noven::scanner::InventoryMatchMode::BestEffort
            && !noisy.selectedItemId.empty(),
        "badly OCR'd long title yields a nearest item");

    // 回归：前缀损坏且低分时，仍应显示目录规范名称而不是 OCR_ONLY 原文。
    // Regression: a damaged low-scoring prefix still shows a canonical name, not OCR_ONLY text.
    auto camelbak = Resolve(catalog, "amelbakTr'-'Zo突击背包（", 200);
    Require(camelbak.selectedItemId.empty(), "damaged Camelbak title fails strict matching");
    camelbak.cropConfidence = 0.1F;
    const auto camelbak_start = std::chrono::steady_clock::now();
    noven::scanner::BestEffortResolve(camelbak, catalog);
    const auto camelbak_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - camelbak_start).count();
    const auto camelbak_economy = noven::scanner::ResolveInventoryEconomy(
        camelbak, empty_store, noven::data::GameMode::Pvp);
    const auto camelbak_display = noven::scanner::BuildInventoryDisplayResult(
        camelbak, camelbak_economy, noven::data::GameMode::Pvp);
    Require(camelbak.matchMode == noven::scanner::InventoryMatchMode::BestEffort
            && (camelbak.selectedItemId == "545cdae64bdc2d39198b4568"
                || camelbak.selectedItemId == "66b5f22b78bbc0200425f904")
            && camelbak_economy.itemId == camelbak.selectedItemId
            && camelbak_display.has_value()
            && camelbak_display->itemId == camelbak.selectedItemId
            && camelbak_display->displayName.find("Camelbak Tri-Zip") != std::string::npos
            && camelbak_display->displayName != camelbak.assembledText
            && camelbak_display->matchQuality == noven::overlay::MatchQuality::LowConfidence,
        "damaged Camelbak OCR resolves a stable ID and canonical title even with low crop confidence");
    Require(camelbak.bestEffortAmbiguous
            && camelbak.bestEffortTop[0].score < 0.5F,
        "low-scoring near-tied Camelbak variants still resolve with ambiguity metadata");
    std::cout << "Camelbak full-catalog fallback ms=" << camelbak_ms
        << " top1=" << camelbak.selectedItemId
        << " top2=" << camelbak.bestEffortTop[1].match.item->id << '\n';

    auto missing_first_fallback = noven::scanner::BuildInventoryRecognition(
        187, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("esert Tech MDR 5.56x45 assault rifle")}
    );
    noven::scanner::ResolveInventoryCatalog(missing_first_fallback, catalog, 1.01F);
    Require(missing_first_fallback.selectedItemId.empty(),
        "high strict threshold rejects the noisy title");
    noven::scanner::BestEffortResolve(missing_first_fallback, catalog);
    Require(missing_first_fallback.selectedItemId == mdr_full.selectedItemId,
        "best-effort recovers a title missing its first character");

    const auto fixture_path = std::filesystem::temp_directory_path()
        / "noven_inventory_best_effort_test.tsv";
    {
        std::ofstream fixture(fixture_path, std::ios::binary);
        Require(static_cast<bool>(fixture), "best-effort fixture can be created");
        fixture << "id\tnameZh\tshortNameZh\tnameEn\tshortNameEn\twidth\theight\n"
            << "111111111111111111111111\tNL545 GP 5.45x39 突击步枪\tNL545\tNL545 GP 5.45x39 assault rifle\tNL545\t2\t1\n"
            << "222222222222222222222222\tNL545 GP 7.62x39 突击步枪\tNL545\tNL545 GP 7.62x39 assault rifle\tNL545\t2\t1\n"
            << "333333333333333333333333\tGP 步枪配件\tGP\tGP rifle part\tGP\t1\t1\n"
            << "444444444444444444444444\tRotor 43 7.62x39 消音器\tR43\tRotor 43 7.62x39 suppressor\tR43\t1\t1\n"
            << "555555555555555555555555\tRotor 43 5.45x39 消音器\tR43\tRotor 43 5.45x39 suppressor\tR43\t1\t1\n"
            << "666666666666666666666666\tAK-74N 5.45x39 突击步枪\tAK-74N\tAK-74N 5.45x39 assault rifle\tAK-74N\t2\t1\n"
            << "777777777777777777777777\tM4A1 5.56x45 突击步枪\tM4A1\tM4A1 5.56x45 assault rifle\tM4A1\t2\t1\n"
            << "888888888888888888888888\tMP5 9x19 冲锋枪\tMP5\tMP5 9x19 SMG\tMP5\t2\t1\n"
            << "999999999999999999999999\tMP-153 12/70 霰弹枪\tMP-153\tMP-153 12/70 shotgun\tMP-153\t2\t1\n";
    }
    noven::data::ItemCatalog small_catalog;
    Require(small_catalog.Load(fixture_path, error), "model/caliber catalog loads");
    auto mixed_fallback = noven::scanner::BuildInventoryRecognition(
        188, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("NL545 GP 545x39 突击步枪")}
    );
    noven::scanner::ResolveInventoryCatalog(mixed_fallback, small_catalog, 1.01F);
    Require(mixed_fallback.selectedItemId.empty(), "strict threshold remains unchanged");
    noven::scanner::BestEffortResolve(mixed_fallback, small_catalog);
    Require(mixed_fallback.selectedItemId == "111111111111111111111111",
        "model and caliber tokens rank the corresponding mixed-language rifle first");
    Require(mixed_fallback.bestEffortTop.size() == 3
            && mixed_fallback.bestEffortTop.front().modelEvidence > 0.0F,
        "best-effort provides top-three diagnostics with numeric evidence");

    auto real_title = noven::scanner::BuildInventoryRecognition(
        190, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("NL545 (GP) 5.45x39 突击步枪")}
    );
    noven::scanner::ResolveInventoryCatalog(real_title, small_catalog, 1.01F);
    noven::scanner::BestEffortResolve(real_title, small_catalog);
    Require(real_title.selectedItemId == "111111111111111111111111",
        "full NL545 title selects same-model same-caliber rifle");
    Require(real_title.bestEffortTop.front().caliberEvidence > 0.0F
            && real_title.bestEffortTop.front().modelEvidence > 0.0F,
        "complete caliber and model contribute separate evidence");
    Require(real_title.bestEffortTop.front().numericEvidence == 0.0F,
        "shared caliber digits do not count as generic numeric evidence");
    for (const auto& candidate : real_title.bestEffortTop) {
        if (candidate.match.item->id == "222222222222222222222222")
            Require(candidate.caliberConflict
                    && candidate.score < real_title.bestEffortTop.front().score,
                "conflicting 7.62x39 loses despite same model and x39 suffix");
        if (candidate.match.item->id == "666666666666666666666666")
            Require(candidate.score < real_title.bestEffortTop.front().score,
                "exact NL545 model outranks different rifle with same caliber");
    }
    const auto rifle_display = noven::scanner::BuildInventoryDisplayResult(
        real_title, {}, noven::data::GameMode::Pvp);
    Require(rifle_display.has_value()
            && rifle_display->matchQuality == noven::overlay::MatchQuality::LowConfidence,
        "valid best-effort rifle still builds a visible result");

    auto suppressor = noven::scanner::BuildInventoryRecognition(
        193, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("Rotor 43 5.45x39 消音器")}
    );
    noven::scanner::ResolveInventoryCatalog(suppressor, small_catalog, 1.01F);
    noven::scanner::BestEffortResolve(suppressor, small_catalog);
    Require(suppressor.selectedItemId == "555555555555555555555555",
        "suppressor OCR still selects same-category same-caliber suppressor");
    const auto check_caliber = [&](std::string text, const char* expected) {
        auto recognized = noven::scanner::BuildInventoryRecognition(
            195, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
            std::nullopt, {Text(std::move(text))}
        );
        noven::scanner::ResolveInventoryCatalog(recognized, small_catalog, 1.01F);
        noven::scanner::BestEffortResolve(recognized, small_catalog);
        Require(recognized.selectedItemId == expected
                && recognized.bestEffortTop.front().caliberEvidence > 0.0F,
            "complete caliber token selects matching item");
    };
    check_caliber("M4A1 5.56x45 突击步枪", "777777777777777777777777");
    check_caliber("MP5 9x19 冲锋枪", "888888888888888888888888");
    check_caliber("MP-153 12/70 霰弹枪", "999999999999999999999999");

    auto clipped_caliber = noven::scanner::BuildInventoryRecognition(
        191, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("45x39")}
    );
    noven::scanner::ResolveInventoryCatalog(clipped_caliber, small_catalog, 1.01F);
    noven::scanner::BestEffortResolve(clipped_caliber, small_catalog);
    Require(clipped_caliber.matchMode == noven::scanner::InventoryMatchMode::BestEffort
            && !clipped_caliber.selectedItemId.empty(),
        "clipped caliber still selects catalog top1 at low confidence");

    const auto contradictory_path = std::filesystem::temp_directory_path()
        / "noven_inventory_contradictory_test.tsv";
    {
        std::ofstream fixture(contradictory_path, std::ios::binary);
        Require(static_cast<bool>(fixture), "contradictory fixture can be created");
        fixture << "id\tnameZh\tshortNameZh\tnameEn\tshortNameEn\twidth\theight\n"
                << "444444444444444444444444\tRotor 43 7.62x39 消音器\tR43\tRotor 43 7.62x39 suppressor\tR43\t1\t1\n";
    }
    noven::data::ItemCatalog contradictory_catalog;
    Require(contradictory_catalog.Load(contradictory_path, error),
        "contradictory-only catalog loads");
    auto contradictory = noven::scanner::BuildInventoryRecognition(
        194, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("NL545 (GP) 5.45x39 突击步枪")}
    );
    noven::scanner::ResolveInventoryCatalog(contradictory, contradictory_catalog, 1.01F);
    noven::scanner::BestEffortResolve(contradictory, contradictory_catalog);
    Require(contradictory.matchMode == noven::scanner::InventoryMatchMode::BestEffort
            && contradictory.selectedItemId == "444444444444444444444444"
            && contradictory.bestEffortTop.front().caliberConflict
            && contradictory.bestEffortTop.front().categoryConflict,
        "conflicting caliber and category penalize but do not suppress catalog top1");
    {
        std::ofstream fixture(contradictory_path, std::ios::binary);
        fixture << "id\tnameZh\tshortNameZh\tnameEn\tshortNameEn\twidth\theight\n"
                << "555555555555555555555555\tRotor 43 5.45x39 消音器\tR43\tRotor 43 5.45x39 suppressor\tR43\t1\t1\n";
    }
    noven::data::ItemCatalog same_caliber_wrong_category;
    Require(same_caliber_wrong_category.Load(contradictory_path, error),
        "same-caliber contradictory catalog loads");
    auto category_only = noven::scanner::BuildInventoryRecognition(
        198, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("NL545 (GP) 5.45x39 突击步枪")}
    );
    noven::scanner::ResolveInventoryCatalog(category_only, same_caliber_wrong_category, 1.01F);
    noven::scanner::BestEffortResolve(category_only, same_caliber_wrong_category);
    Require(category_only.matchMode == noven::scanner::InventoryMatchMode::BestEffort
            && category_only.selectedItemId == "555555555555555555555555"
            && category_only.bestEffortTop.front().categoryConflict
            && !category_only.bestEffortTop.front().caliberConflict,
        "category contradiction remains visible in low-confidence ranking");
    std::filesystem::remove(contradictory_path);

    auto real_catalog_title = noven::scanner::BuildInventoryRecognition(
        192, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("NL545 (GP) 5.45x39 突击步枪")}
    );
    auto nl545_strict = real_catalog_title;
    noven::scanner::ResolveInventoryCatalog(nl545_strict, catalog, 0.64F);
    Require(nl545_strict.selectedItemId == "68c2940aecc41cc5490bd40e",
        "real NL545 OCR now resolves the canonical stable ID strictly");
    noven::scanner::ResolveInventoryCatalog(real_catalog_title, catalog, 1.01F);
    noven::scanner::BestEffortResolve(real_catalog_title, catalog);
    Require(real_catalog_title.matchMode == noven::scanner::InventoryMatchMode::BestEffort
            && real_catalog_title.selectedItemId == "68c2940aecc41cc5490bd40e",
        "NL545 leads best-effort ranking after catalog regeneration");
    const auto nl545_economy = noven::scanner::ResolveInventoryEconomy(
        nl545_strict, empty_store, noven::data::GameMode::Pvp);
    const auto nl545_display = noven::scanner::BuildInventoryDisplayResult(
        nl545_strict, nl545_economy, noven::data::GameMode::Pvp);
    Require(nl545_economy.itemId == "68c2940aecc41cc5490bd40e"
            && nl545_display.has_value()
            && nl545_display->itemId == nl545_economy.itemId,
        "NL545 stable ID crosses the economy and display boundary unchanged");
    auto real_clipped = noven::scanner::BuildInventoryRecognition(
        197, noven::scanner::InventoryRecognitionPath::AdaptiveFallback,
        std::nullopt, {Text("45x39")}
    );
    noven::scanner::ResolveInventoryCatalog(real_clipped, catalog, 0.64F);
    noven::scanner::BestEffortResolve(real_clipped, catalog);
    Require(real_clipped.matchMode == noven::scanner::InventoryMatchMode::BestEffort
            && !real_clipped.selectedItemId.empty(),
        "actual clipped scan-19 OCR receives a low-confidence catalog candidate");
    std::cout << "NL545 real catalog mode="
        << (real_catalog_title.matchMode == noven::scanner::InventoryMatchMode::OcrOnly
            ? "OCR_ONLY" : "BEST_EFFORT") << '\n';
    for (const auto& candidate : real_catalog_title.bestEffortTop) {
        std::cout << "  " << candidate.match.item->id << ' '
            << candidate.match.matchedAlias << " score=" << candidate.score
            << " caliber=" << candidate.candidateCaliber
            << " category=" << candidate.candidateCategory << '\n';
    }
    auto rbav = noven::scanner::BuildInventoryRecognition(
        196, noven::scanner::InventoryRecognitionPath::PrimaryTooltip,
        std::nullopt, {Text("ECLiPSE RBAV-AF plate carrier")}
    );
    noven::scanner::ResolveInventoryCatalog(rbav, catalog, 1.01F);
    noven::scanner::BestEffortResolve(rbav, catalog);
    Require(!rbav.bestEffortTop.empty()
            && rbav.bestEffortTop.front().modelEvidence > 0.0F,
        "hyphenated RBAV-AF model code remains intact");

    auto clipped_rbav = Resolve(catalog, "CLPSERBAV-'AF插板胸挂（从林绿", 198);
    if (!clipped_rbav.selectedCandidate.has_value()) {
        clipped_rbav.cropConfidence = 0.9F;
        noven::scanner::BestEffortResolve(clipped_rbav, catalog);
    }
    const auto clipped_rbav_display = noven::scanner::BuildInventoryDisplayResult(
        clipped_rbav, {}, noven::data::GameMode::Pvp);
    Require(clipped_rbav_display.has_value()
            && clipped_rbav_display->itemId == "628dc750b910320f4c27a732"
            && clipped_rbav_display->displayName == "ECLiPSE RBAV-AF 插板胸挂（丛林绿）"
            && clipped_rbav_display->displayName != clipped_rbav.assembledText,
        "partial RBAV OCR displays canonical catalog name");

    auto clipped_junk_box = Resolve(catalog, "av垃圾箱", 199);
    if (!clipped_junk_box.selectedCandidate.has_value()) {
        clipped_junk_box.cropConfidence = 0.9F;
        noven::scanner::BestEffortResolve(clipped_junk_box, catalog);
    }
    const auto clipped_junk_display = noven::scanner::BuildInventoryDisplayResult(
        clipped_junk_box, {}, noven::data::GameMode::Pvp);
    Require(clipped_junk_display.has_value()
            && clipped_junk_display->itemId == "5b7c710788a4506dec015957"
            && clipped_junk_display->displayName == "幸运Scav垃圾箱"
            && clipped_junk_display->displayName != clipped_junk_box.assembledText,
        "clipped Chinese suffix retrieves and displays canonical catalog item");

    const std::string unindexed = "ZZZXQ 998877 unknown";
    Require(small_catalog.MatchDiagnostics(unindexed, 3).empty(),
        "unindexed OCR produces no normal candidates");
    Require(small_catalog.MatchBestEffort(unindexed, 3).size() == 3,
        "best-effort scans all aliases if indexed retrieval is empty");
    std::filesystem::remove(fixture_path);

    noven::data::ItemCatalog unavailable_catalog;
    for (const std::string noise : {"-", "' - '", "232/400", "246"}) {
        auto noisy_scan = noven::scanner::BuildInventoryRecognition(
            200, noven::scanner::InventoryRecognitionPath::AdaptiveFallback,
            std::nullopt, {Text(noise)});
        noven::scanner::ResolveInventoryCatalog(noisy_scan, catalog, 0.64F);
        noven::scanner::BestEffortResolve(noisy_scan, catalog);
        Require(!noisy_scan.selectedCandidate.has_value()
                && noisy_scan.selectedItemId.empty()
                && noisy_scan.bestEffortTop.empty()
                && noisy_scan.matchMode == noven::scanner::InventoryMatchMode::OcrOnly
                && noisy_scan.rejectionReason == "no_item_name",
            "punctuation and inventory counters never become arbitrary catalog items");
    }
    auto ocr_only = Resolve(unavailable_catalog, "识别到的文本");
    noven::scanner::BestEffortResolve(ocr_only, unavailable_catalog);
    const auto ocr_only_display = noven::scanner::BuildInventoryDisplayResult(
        ocr_only, {}, noven::data::GameMode::Pvp);
    Require(ocr_only.matchMode == noven::scanner::InventoryMatchMode::OcrOnly
            && ocr_only_display.has_value()
            && ocr_only_display->itemId.empty()
            && ocr_only_display->displayName == "识别到的文本"
            && ocr_only_display->matchQuality == noven::overlay::MatchQuality::OcrOnly,
        "nonempty OCR with unavailable catalog still yields OCR-only card");

    std::cout << "Inventory recognition tests passed\n";
    return 0;
}
