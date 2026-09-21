#pragma once

#include <cstddef>
#include <filesystem>
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

    [[nodiscard]] std::vector<ItemMatch> MatchDiagnostics(
        std::string_view text,
        std::size_t maximum_candidates = 10
    ) const;

    [[nodiscard]] bool IsConfidentMatch(
        std::string_view text,
        const ItemMatch& match
    ) const noexcept;

    [[nodiscard]] std::size_t ItemCount() const noexcept { return items_.size(); }
    [[nodiscard]] std::size_t AliasCount() const noexcept { return aliases_.size(); }

private:
    struct AliasEntry final {
        std::size_t item_index{};
        std::size_t item_alias_index{};
        std::string normalized;
        AliasType type{};
    };

    void BuildIndexes();
    void AddAlias(std::size_t item_index, std::string text, Language language, AliasType type);

    [[nodiscard]] std::vector<ItemMatch> RankMatches(std::string_view text) const;

    std::vector<ItemRecord> items_;
    std::vector<AliasEntry> aliases_;
    std::unordered_map<std::string, std::vector<std::size_t>> exact_index_;
    std::unordered_map<std::string, std::vector<std::size_t>> canonical_index_;
    std::unordered_map<char32_t, std::vector<std::size_t>> fuzzy_index_;
};

} // namespace noven::data
