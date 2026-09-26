#include "data/ItemCatalog.h"

#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <chrono>
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

const noven::data::ItemMatch* Find(
    const std::vector<noven::data::ItemMatch>& matches,
    const std::string& id
) {
    for (const auto& match : matches) {
        if (match.item != nullptr && match.item->id == id) {
            return &match;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "noven_item_catalog_test.tsv";
    {
        std::ofstream file(path, std::ios::binary);
        Require(static_cast<bool>(file), "test catalog can be created");
        file << "# test catalog\n"
             << "id\tnameZh\tshortNameZh\tnameEn\tshortNameEn\twidth\theight\n"
             << "5df8a2ca86f7740bfe6df777\t6B2 防弹衣（丛林迷彩）\t6B2\t6B2 body armor (Flora)\t6B2\t2\t1\n"
             << "61bf7b6302b3924be92fa8c3\t金属零件\t金属零件\tMetal spare parts\tM.parts\t1\t1\n"
             << "5447a9cd4bdc2dbd208b4567\tM4A1 中文全名\tM4A1\tColt M4A1 5.56x45 assault rifle\tM4A1\t2\t1\n"
             << "5447ac644bdc2d6c208b4567\tM4A1\t另一个短名\tAnother item\tOther\t1\t1\n";
    }
    {
        std::ofstream file(path, std::ios::app | std::ios::binary);
        Require(static_cast<bool>(file), "additional test catalog rows can be appended");
        file << "61bf7b6302b3924be92fa8c4\t\xE9\x87\x91\xE5\xB1\x9E\xE9\x9B\xB6\xE4\xBB\xB6\t\xE9\x87\x91\xE5\xB1\x9E\xE9\x9B\xB6\xE4\xBB\xB6\tMetal parts\tParts\t1\t1\n"
             << "5c48a2c22e221602b313fb6c\tMDR grip\tMDR\tMDR pistol grip (FDE)\tMDR\t1\t1\n"
             << "5c488a752e221602b412af63\tMDR rifle\tMDR\tDesert Tech MDR 5.56x45 assault rifle\tMDR\t2\t1\n"
             << "545cdae64bdc2d39198b4568\tCamelbak Tri-Zip 突击背包（叶绿色）\tTri-Zip\tCamelbak Tri-Zip assault backpack (Foliage)\tTri-Zip\t2\t3\n"
             << "5c0a840b86f7747fa141986d\tTHICC 物品箱\tTHICC\tT H I C C item case\tT H I C C\t4\t3\n";
    }

    noven::data::ItemCatalog catalog;
    std::wstring error;
    Require(catalog.Load(path, error), "catalog loads");
    Require(catalog.ItemCount() == 9, "all test items load");
    Require(catalog.AliasCount() == 36, "all aliases are indexed");
    Require(catalog.FindById("545cdae64bdc2d39198b4568") != nullptr
            && catalog.FindById("545cdae64bdc2d39198b4568")->shortNameZh == "Tri-Zip"
            && catalog.FindById("missing") == nullptr,
        "stable ID retrieves canonical short-name snapshot without matching");

    const auto zh_name = catalog.Match("金属零件");
    Require(Find(zh_name, "61bf7b6302b3924be92fa8c3") != nullptr,
        "Chinese full name matches");
    const auto zh_short = catalog.Match("6B2");
    Require(Find(zh_short, "5df8a2ca86f7740bfe6df777") != nullptr,
        "Chinese short name matches");

    const auto english_name = catalog.Match("6B2 body armor (Flora)");
    const auto* english_match = Find(english_name, "5df8a2ca86f7740bfe6df777");
    Require(english_match != nullptr, "English full name matches");
    Require(english_match->matchType == noven::data::MatchType::ExactName,
        "English full name uses exact match");

    const auto english_short = catalog.Match("M.parts");
    Require(Find(english_short, "61bf7b6302b3924be92fa8c3") != nullptr,
        "English short name matches");

    Require(Find(catalog.Match("COLT M4A1 5.56X45 ASSAULT RIFLE"),
                 "5447a9cd4bdc2dbd208b4567") != nullptr,
        "English case differences match");
    Require(Find(catalog.Match("  Metal   spare   parts  "),
                 "61bf7b6302b3924be92fa8c3") != nullptr,
        "repeated whitespace matches");
    Require(Find(catalog.Match("６Ｂ２ body armor （Flora）"),
                 "5df8a2ca86f7740bfe6df777") != nullptr,
        "full-width numbers and punctuation match");
    const auto cjk_punctuation = catalog.Match(
        "CamelBak Tri-Zip 突击背包 (叶绿色)"
    );
    Require(!cjk_punctuation.empty()
            && cjk_punctuation.front().item->id == "545cdae64bdc2d39198b4568",
        "Chinese and ASCII parentheses normalize to the same mixed-language alias");
    const auto camelbak_english = catalog.Match(
        "Camelbak Tri-Zip assault backpack (Foliage)"
    );
    Require(!camelbak_english.empty()
            && camelbak_english.front().item->id == "545cdae64bdc2d39198b4568",
        "Chinese and English full aliases resolve to one stable item ID");
    const auto damaged_camelbak = catalog.Match("amelbakTr'-'Zio", 10);
    Require(!damaged_camelbak.empty()
            && damaged_camelbak.front().item->id == "545cdae64bdc2d39198b4568",
        "fragmented OCR typo ranks the Camelbak English catalog alias first");
    const auto thicc_ocr = catalog.Match("HICC 物品箱", 10);
    Require(!thicc_ocr.empty()
            && thicc_ocr.front().item->id == "5c0a840b86f7747fa141986d",
        "missing leading OCR character ranks the THICC Chinese catalog item first");

    const auto typo = catalog.Match("6B2 bodi armor (Fora)");
    const auto* typo_match = Find(typo, "5df8a2ca86f7740bfe6df777");
    Require(typo_match != nullptr, "common OCR typo uses fuzzy matching");
    Require(typo_match->matchType == noven::data::MatchType::FuzzyName,
        "common OCR typo is marked fuzzy");
    Require(typo_match->score > 0.80F, "common OCR typo has a strong score");

    Require(catalog.Match("not a Tarkov item").empty(),
        "unknown text has no accepted result");

    const auto collision = catalog.Match("M4A1", 5);
    const auto* canonical_name = Find(collision, "5447ac644bdc2d6c208b4567");
    const auto* colliding_short = Find(collision, "5447a9cd4bdc2dbd208b4567");
    Require(canonical_name != nullptr && colliding_short != nullptr,
        "colliding aliases retain both stable IDs");
    Require(collision.front().item->id == "5447ac644bdc2d6c208b4567",
        "exact full-name alias outranks short-name collision");

    const auto mdr = catalog.Match("MDR", 10);
    const auto* mdr_grip = Find(mdr, "5c48a2c22e221602b313fb6c");
    const auto* mdr_rifle = Find(mdr, "5c488a752e221602b412af63");
    Require(mdr_grip != nullptr && mdr_rifle != nullptr,
        "MDR family aliases retain both stable IDs");
    Require(mdr.front().ambiguous && !catalog.IsConfidentMatch("MDR", mdr.front()),
        "short common MDR token is ambiguous");
    Require(mdr.front().competitiveCandidateCount > 1
            && mdr.front().scoreGap < 0.08F,
        "MDR exposes competing candidates and a small score gap");

    noven::data::CatalogNarrowingStats size_stats;
    const auto sized_mdr = catalog.MatchConstrained(
        "MDR",
        10,
        0.64F,
        noven::data::ItemDimensions{2, 1},
        &size_stats
    );
    Require(sized_mdr.size() == 1
            && sized_mdr.front().item->id == "5c488a752e221602b412af63",
        "reliable 2x1 dimensions narrow MDR aliases to the rifle");
    Require(size_stats.allItems == 9 && size_stats.afterSizeFilter == 3
            && size_stats.afterAliasFilter == 1,
        "catalog narrowing reports size and alias candidate counts");
    const auto unknown_size_mdr = catalog.MatchConstrained(
        "MDR",
        10,
        0.64F,
        std::nullopt
    );
    Require(unknown_size_mdr.size() >= 2,
        "unknown dimensions do not reject otherwise valid item aliases");

    const auto mdr_richer = catalog.Match("MDR 5.56x45", 10);
    const auto* richer_rifle = Find(mdr_richer, "5c488a752e221602b412af63");
    Require(richer_rifle != nullptr && richer_rifle->score > 0.80F,
        "richer MDR and caliber evidence selects the rifle family");
    Require(catalog.IsConfidentMatch("MDR 5.56x45", *richer_rifle),
        "richer MDR evidence is confident");

    const auto grip_full = catalog.Match("MDR pistol grip (FDE)");
    const auto* grip_full_match = Find(grip_full, "5c48a2c22e221602b313fb6c");
    Require(grip_full_match != nullptr
            && grip_full_match->matchType == noven::data::MatchType::ExactName
            && catalog.IsConfidentMatch("MDR pistol grip (FDE)", *grip_full_match),
        "full MDR grip name remains an exact confident match");

    const auto chinese_short = catalog.Match("\xE9\x87\x91\xE5\xB1\x9E\xE9\x9B\xB6\xE4\xBB\xB6");
    const auto* chinese_short_match = Find(chinese_short, "61bf7b6302b3924be92fa8c4");
    Require(chinese_short_match != nullptr
            && !chinese_short_match->ambiguous
            && catalog.IsConfidentMatch("\xE9\x87\x91\xE5\xB1\x9E\xE9\x9B\xB6\xE4\xBB\xB6", *chinese_short_match),
        "unique short Chinese name remains confident");

    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    Require(!remove_error, "test catalog is removed");

    if (argc > 1) {
        noven::data::ItemCatalog production_catalog;
        std::wstring production_error;
        Require(production_catalog.Load(
            std::filesystem::path(argv[1]),
            production_error
        ),
            "production catalog loads for benchmark");
        constexpr auto kNl545 = "68c2940aecc41cc5490bd40e";
        Require(production_catalog.ItemCount() >= 5'400
                && production_catalog.AliasCount() >= 20'000
                && production_catalog.EnglishFieldCount() >= 10'000
                && production_catalog.ChineseFieldCount() >= 10'000
                && !production_catalog.SourceVersion().empty()
                && !production_catalog.GeneratedAt().empty(),
            "production catalog is current and records generation health");
        const auto nl545_english = production_catalog.Match(
            "Custom Guns NL545 (GP) 5.45x39 assault rifle", 20);
        const auto nl545_chinese = production_catalog.Match(
            "NL545 (GP) 5.45x39 突击步枪", 20);
        const auto nl545_short = production_catalog.Match("NL545 GP", 20);
        const auto* rifle = Find(nl545_english, kNl545);
        Require(rifle != nullptr && Find(nl545_chinese, kNl545) != nullptr
                && Find(nl545_short, kNl545) != nullptr,
            "NL545 English, Chinese, and short aliases retain one stable ID");
        Require(rifle->item->width == 1 && rifle->item->height == 1
                && rifle->item->caliber == "Caliber545x39"
                && std::find(rifle->item->types.begin(), rifle->item->types.end(), "gun")
                    != rifle->item->types.end(),
            "NL545 dimensions, caliber, and canonical item type are retained");
        const auto start = std::chrono::steady_clock::now();
        for (int iteration = 0; iteration < 100; ++iteration) {
            static_cast<void>(production_catalog.Match("6B2 bodi armor (Fora)"));
        }
        const double elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start
        ).count();
        std::cout << "Production fuzzy matching average ms="
                  << elapsed_ms / 100.0 << '\n';
    }
    std::cout << "ItemCatalog tests passed\n";
    return 0;
}
