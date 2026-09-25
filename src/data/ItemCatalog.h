#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace noven::data {

enum class Language {
    Zh,
    En,
};

enum class AliasType {
    Name,
    ShortName,
};

struct ItemAlias final {
    std::string text;
    Language language{};
    AliasType type{};
};

struct ItemRecord final {
    std::string id;
    std::string nameZh;
    std::string shortNameZh;
    std::string nameEn;
    std::string shortNameEn;
    int width{};
    int height{};
    std::vector<std::string> types;
    std::string caliber;
    std::vector<ItemAlias> aliases;
};

enum class MatchType {
    ExactName,
    ExactShortName,
    CanonicalName,
    CanonicalShortName,
    FuzzyName,
    FuzzyShortName,
};

struct ItemMatch final {
    const ItemRecord* item{};
    std::string matchedAlias;
    MatchType matchType{};
    float score{};
    float bestScore{};
    float secondBestScore{};
    float scoreGap{};
    std::size_t competitiveCandidateCount{};
    bool ambiguous{};
};

struct ItemDimensions final {
    int width{};
    int height{};
};

struct CatalogNarrowingStats final {
    std::size_t allItems{};
    std::size_t afterSizeFilter{};
    std::size_t afterAliasFilter{};
};

[[nodiscard]] std::string NormalizeForMatching(std::string_view text);
[[nodiscard]] const char* MatchTypeName(MatchType type) noexcept;

class ItemCatalog final {
public:
    bool Load(const std::filesystem::path& path, std::wstring& error);

    [[nodiscard]] std::vector<ItemMatch> Match(
        std::string_view text,
        std::size_t maximum_candidates = 5,
        float acceptance_threshold = 0.64F
    ) const;

    [[nodiscard]] std::vector<ItemMatch> MatchConstrained(
        std::string_view text,
        std::size_t maximum_candidates,
        float acceptance_threshold,
        std::optional<ItemDimensions> size_constraint = std::nullopt,
        CatalogNarrowingStats* narrowing_stats = nullptr
    ) const;

    [[nodiscard]] std::vector<ItemMatch> MatchDiagnostics(
        std::string_view text,
        std::size_t maximum_candidates = 10
    ) const;

    // Fallback-only: indexed retrieval first; full scan if fewer than three candidates.
    [[nodiscard]] std::vector<ItemMatch> MatchBestEffort(
        std::string_view text,
        std::size_t maximum_candidates = 10
    ) const;

    [[nodiscard]] std::vector<ItemMatch> MatchDiagnosticsConstrained(
        std::string_view text,
        std::size_t maximum_candidates,
        std::optional<ItemDimensions> size_constraint = std::nullopt,
        CatalogNarrowingStats* narrowing_stats = nullptr
    ) const;

    [[nodiscard]] bool IsConfidentMatch(
        std::string_view text,
        const ItemMatch& match
    ) const noexcept;

    [[nodiscard]] std::size_t ItemCount() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t AliasCount() const noexcept { return aliases_.size(); }
    [[nodiscard]] std::size_t EnglishFieldCount() const noexcept { return english_fields_; }
    [[nodiscard]] std::size_t ChineseFieldCount() const noexcept { return chinese_fields_; }
    [[nodiscard]] const std::string& SourceVersion() const noexcept { return source_version_; }
    [[nodiscard]] const std::string& GeneratedAt() const noexcept { return generated_at_; }

private:
    struct AliasEntry final {
        std::size_t item_index{};
        std::size_t item_alias_index{};
        std::string normalized;
        AliasType type{};
    };

    void BuildIndexes();
    void AddAlias(std::size_t item_index, std::string text, Language language, AliasType type);

    [[nodiscard]] std::vector<ItemMatch> RankMatches(
        std::string_view text,
        std::optional<ItemDimensions> size_constraint,
        CatalogNarrowingStats* narrowing_stats
    ) const;

    std::vector<ItemRecord> items_;
    std::vector<AliasEntry> aliases_;
    std::unordered_map<std::string, std::vector<std::size_t>> exact_index_;
    std::unordered_map<std::string, std::vector<std::size_t>> canonical_index_;
    std::unordered_map<char32_t, std::vector<std::size_t>> fuzzy_index_;
    std::unordered_map<std::string, std::vector<std::size_t>> fuzzy_token_index_;
    std::size_t english_fields_{};
    std::size_t chinese_fields_{};
    std::string source_version_;
    std::string generated_at_;
};

} // namespace noven::data
