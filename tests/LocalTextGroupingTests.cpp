#include "data/ItemCatalog.h"
#include "scanner/LocalTextGrouping.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

noven::ocr::RecognizedText Text(
    std::string value,
    float x1,
    float y1,
    float x2,
    float y2,
    float confidence = 0.95F
) {
    return noven::ocr::RecognizedText{
        noven::ocr::TextBox{x1, y1, x2, y2, confidence},
        std::move(value),
        confidence,
    };
}

noven::data::ItemCatalog LoadCatalog() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "noven_local_text_grouping_catalog.tsv";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        Require(static_cast<bool>(file), "grouping catalog can be created");
        file << "id\tnameZh\tshortNameZh\tnameEn\tshortNameEn\twidth\theight\n"
             << "61bf7b6302b3924be92fa8c3\t金属零件\t金属\tMetal spare parts\tM.parts\t1\t1\n"
             << "64abd93857958b4249003418\tInterceptor OTV防弹衣\tOTV\tInterceptor OTV body armor (Woodland)\tOTV\t2\t2\n"
             << "5df8a2ca86f7740bfe6df777\t6B2 防弹衣\t6B2\t6B2 body armor (Flora)\t6B2\t2\t2\n"
             << "60361a7497633951dc245eb4\tUCP军帽\tUCP\tArmy cap (UCP)\tUCP\t1\t1\n";
    }
    noven::data::ItemCatalog catalog;
    std::wstring error;
    Require(catalog.Load(path, error), "grouping catalog loads");
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    Require(!remove_error, "grouping catalog is removed");
    return catalog;
}

void SameLineTwoBoxes(const noven::data::ItemCatalog& catalog) {
    const std::vector<noven::ocr::RecognizedText> texts{
        Text("Interceptor OTV", 100.0F, 100.0F, 230.0F, 126.0F),
        Text("body armor (Woodland)", 238.0F, 101.0F, 430.0F, 127.0F),
    };
    const auto groups = noven::scanner::BuildLocalTextGroups(texts, {});
    Require(groups.size() == 1, "two same-line boxes produce one local group");
    Require(
        groups.front().combinedText == "Interceptor OTV body armor (Woodland)",
        "same-line boxes use left-to-right reading order"
    );
    const auto matches = noven::scanner::MatchLocalTextGroups(catalog, texts, 0.64F);
    Require(matches.size() == 1, "two-box English item name matches catalog");
    Require(
        matches.front().match.item->id == "64abd93857958b4249003418",
        "two-box match keeps item ID"
    );
}

void SameLineThreeBoxes(const noven::data::ItemCatalog& catalog) {
    const std::vector<noven::ocr::RecognizedText> texts{
        Text("Interceptor", 100.0F, 100.0F, 190.0F, 126.0F),
        Text("OTV body", 198.0F, 101.0F, 275.0F, 127.0F),
        Text("armor (Woodland)", 283.0F, 100.0F, 435.0F, 126.0F),
    };
    const auto groups = noven::scanner::BuildLocalTextGroups(texts, {});
    const auto exact_group = std::find_if(
        groups.begin(),
        groups.end(),
        [](const noven::scanner::LocalTextGroup& group) {
            return group.combinedText == "Interceptor OTV body armor (Woodland)";
        }
    );
    Require(exact_group != groups.end(), "three-box reading order is preserved");
    const auto matches = noven::scanner::MatchLocalTextGroups(catalog, texts, 0.64F);
    const auto exact_match = std::find_if(
        matches.begin(),
        matches.end(),
        [](const noven::scanner::TextGroupMatch& match) {
            return match.combinedText == "Interceptor OTV body armor (Woodland)";
        }
    );
    Require(exact_match != matches.end(), "three same-line boxes match as one item");
    Require(
        exact_match->match.item->id == "64abd93857958b4249003418",
        "three-box match keeps item ID"
    );
}

void StackedBoxes(const noven::data::ItemCatalog& catalog) {
    const std::vector<noven::ocr::RecognizedText> texts{
        Text("Interceptor OTV body", 100.0F, 100.0F, 290.0F, 126.0F),
        Text("armor (Woodland)", 104.0F, 132.0F, 260.0F, 158.0F),
    };
    const auto matches = noven::scanner::MatchLocalTextGroups(catalog, texts, 0.64F);
    Require(matches.size() == 1, "stacked item-name boxes match");
    Require(
        matches.front().combinedText == "Interceptor OTV body armor (Woodland)",
        "stacked boxes use top-to-bottom reading order"
    );
}

void ChineseBoxes(const noven::data::ItemCatalog& catalog) {
    const std::vector<noven::ocr::RecognizedText> texts{
        Text("金属", 100.0F, 100.0F, 145.0F, 126.0F),
        Text("零件", 150.0F, 100.0F, 195.0F, 126.0F),
    };
    const auto matches = noven::scanner::MatchLocalTextGroups(catalog, texts, 0.64F);
    Require(matches.size() == 1, "Chinese split name matches");
    Require(
        matches.front().match.item->id == "61bf7b6302b3924be92fa8c3",
        "Chinese group keeps item ID"
    );
}

void RejectUnrelatedAndDistant() {
    const std::vector<noven::ocr::RecognizedText> texts{
        Text("Interceptor", 100.0F, 100.0F, 190.0F, 126.0F),
        Text("口袋", 198.0F, 180.0F, 245.0F, 206.0F),
        Text("Woodland", 600.0F, 100.0F, 700.0F, 126.0F),
    };
    const auto groups = noven::scanner::BuildLocalTextGroups(texts, {});
    Require(groups.empty(), "unrelated and distant boxes are not grouped");
}

void GroupedFuzzyMatch(const noven::data::ItemCatalog& catalog) {
    const std::vector<noven::ocr::RecognizedText> texts{
        Text("6B2 bodi", 100.0F, 100.0F, 180.0F, 126.0F),
        Text("armor (Fora)", 188.0F, 100.0F, 300.0F, 126.0F),
    };
    const auto matches = noven::scanner::MatchLocalTextGroups(catalog, texts, 0.64F);
    Require(matches.size() == 1, "grouped English OCR typo remains fuzzy-matchable");
    Require(
        matches.front().match.item->id == "5df8a2ca86f7740bfe6df777",
        "grouped fuzzy match keeps item ID"
    );
}

void SingleExactMatchIsPreferred() {
    Require(
        !noven::scanner::GroupedMatchClearlyBetter(1.0F, 1.0F),
        "a grouped exact match does not replace an equal single exact match"
    );
    Require(
        noven::scanner::GroupedMatchClearlyBetter(1.0F, 0.80F),
        "a clearly better grouped match can replace a weaker single match"
    );
}

void Benchmark(const noven::data::ItemCatalog& catalog) {
    const std::vector<noven::ocr::RecognizedText> texts{
        Text("Interceptor", 100.0F, 100.0F, 190.0F, 126.0F),
        Text("OTV body", 198.0F, 101.0F, 275.0F, 127.0F),
        Text("armor (Woodland)", 283.0F, 100.0F, 435.0F, 126.0F),
        Text("口袋", 500.0F, 400.0F, 550.0F, 426.0F),
    };
    const auto start = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < 1000; ++iteration) {
        static_cast<void>(noven::scanner::MatchLocalTextGroups(catalog, texts, 0.64F));
    }
    const double elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start
    ).count() / 1000.0;
    std::cout << "local grouping average ms=" << elapsed << "\n";
}

} // namespace

int main() {
    try {
        const noven::data::ItemCatalog catalog = LoadCatalog();
        SameLineTwoBoxes(catalog);
        SameLineThreeBoxes(catalog);
        StackedBoxes(catalog);
        ChineseBoxes(catalog);
        RejectUnrelatedAndDistant();
        GroupedFuzzyMatch(catalog);
        SingleExactMatchIsPreferred();
        Benchmark(catalog);
        std::cout << "Local text grouping tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
