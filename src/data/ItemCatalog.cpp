#include "data/ItemCatalog.h"

#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace noven::data {

namespace {

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size
    );
    return result;
}

std::wstring CompatibilityNormalize(std::wstring text) {
    for (wchar_t& character : text) {
        // Keep this mapping deliberately narrow: it covers full-width Latin
        // text and punctuation without removing valid Chinese characters.
        if (character >= L'！' && character <= L'～') {
            character = static_cast<wchar_t>(character - (L'！' - L'!'));
        } else if (character == L'　') {
            character = L' ';
        }
    }
    return text;
}

char32_t NextCodePoint(std::string_view text, std::size_t& offset) {
    const auto byte = [&](std::size_t index) {
        return static_cast<unsigned char>(text[index]);
    };
    const auto continuation = [&]() {
        return static_cast<char32_t>(byte(offset++) & 0x3F);
    };
    const unsigned char first = byte(offset++);
    if (first < 0x80) {
        return first;
    }
    if ((first & 0xE0) == 0xC0 && offset < text.size()) {
        return (static_cast<char32_t>(first & 0x1F) << 6)
            | continuation();
    }
    if ((first & 0xF0) == 0xE0 && offset + 1 < text.size()) {
        return (static_cast<char32_t>(first & 0x0F) << 12)
            | (continuation() << 6)
            | continuation();
    }
    if ((first & 0xF8) == 0xF0 && offset + 2 < text.size()) {
        return (static_cast<char32_t>(first & 0x07) << 18)
            | (continuation() << 12)
            | (continuation() << 6)
            | continuation();
    }
    return 0xFFFD;
}

std::vector<char32_t> CodePoints(std::string_view text) {
    std::vector<char32_t> result;
    std::size_t offset = 0;
    while (offset < text.size()) {
        result.push_back(NextCodePoint(text, offset));
    }
    return result;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr
    );
    return result;
}

wchar_t NormalizePunctuation(wchar_t character) {
    switch (character) {
    case L'，': return L',';
    case L'。': return L'.';
    case L'！': return L'!';
    case L'？': return L'?';
    case L'：': return L':';
    case L'；': return L';';
    case L'、': return L',';
    case L'“':
    case L'”': return L'"';
    case L'‘':
    case L'’': return L'\'';
    case L'‐':
    case L'‑':
    case L'‒':
    case L'–':
    case L'—': return L'-';
    case L'／': return L'/';
    case L'（': return L'('; 
    case L'）': return L')';
    default: return character;
    }
}

std::vector<std::string> SplitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t separator = line.find('\t', start);
        if (separator == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, separator - start));
        start = separator + 1;
    }
}

std::string UnescapeField(std::string value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            if (value[i + 1] == 't') {
                result.push_back('\t');
                ++i;
                continue;
            }
            if (value[i + 1] == 'n') {
                result.push_back('\n');
                ++i;
                continue;
            }
        }
        result.push_back(value[i]);
    }
    return result;
}

bool ValidId(std::string_view id) {
    if (id.size() != 24) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char character) {
        return std::isdigit(character) != 0
            || (character >= 'a' && character <= 'f');
    });
}

int ParseInteger(std::string_view value, bool& valid) {
    if (value.empty()) {
        valid = true;
        return 0;
    }
    int result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    valid = parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
    return result;
}

float EditSimilarity(std::string_view left, std::string_view right) {
    const auto left_points = CodePoints(left);
    const auto right_points = CodePoints(right);
    if (left_points.empty() || right_points.empty()) {
        return 0.0F;
    }
    std::vector<std::size_t> previous(right_points.size() + 1);
    std::vector<std::size_t> current(right_points.size() + 1);
    std::iota(previous.begin(), previous.end(), 0);
    for (std::size_t i = 1; i <= left_points.size(); ++i) {
        current[0] = i;
        for (std::size_t j = 1; j <= right_points.size(); ++j) {
            current[j] = std::min({
                previous[j] + 1,
                current[j - 1] + 1,
                previous[j - 1] + (left_points[i - 1] == right_points[j - 1] ? 0 : 1),
            });
        }
        std::swap(previous, current);
    }
    const std::size_t longest = std::max(left_points.size(), right_points.size());
    return 1.0F - static_cast<float>(previous.back()) / static_cast<float>(longest);
}

bool HasAsciiLetter(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z');
    });
}

std::vector<std::string> EnglishTokens(std::string_view value) {
    std::vector<std::string> tokens;
    std::string token;
    for (const unsigned char character : value) {
        if ((character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9')) {
            token.push_back(static_cast<char>(character));
        } else if (!token.empty()) {
            tokens.push_back(std::move(token));
            token.clear();
        }
    }
    if (!token.empty()) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

float FuzzyScore(std::string_view query, std::string_view alias) {
    const float edit = EditSimilarity(query, alias);
    float score = edit;
    if (HasAsciiLetter(query) && HasAsciiLetter(alias)) {
        const auto query_tokens = EnglishTokens(query);
        const auto alias_tokens = EnglishTokens(alias);
        std::size_t overlap = 0;
        for (const auto& token : query_tokens) {
            if (std::find(alias_tokens.begin(), alias_tokens.end(), token) != alias_tokens.end()) {
                ++overlap;
            }
        }
        const std::size_t union_size = query_tokens.size() + alias_tokens.size() - overlap;
        const float token_score = union_size == 0
            ? 0.0F
            : static_cast<float>(overlap) / static_cast<float>(union_size);
        score = std::max(score, edit * 0.75F + token_score * 0.25F);
    }
    return std::clamp(score, 0.0F, 1.0F);
}

bool ContainsCjk(std::string_view value) {
    for (const char32_t code_point : CodePoints(value)) {
        if ((code_point >= 0x3400 && code_point <= 0x4DBF)
            || (code_point >= 0x4E00 && code_point <= 0x9FFF)
            || (code_point >= 0xF900 && code_point <= 0xFAFF)) {
            return true;
        }
    }
    return false;
}

float TokenEvidenceScore(std::string_view query, std::string_view alias) {
    const auto query_tokens = EnglishTokens(query);
    if (query_tokens.size() < 2) {
        return 0.0F;
    }
    const auto alias_tokens = EnglishTokens(alias);
    if (alias_tokens.empty()) {
        return 0.0F;
    }
    std::size_t overlap = 0;
    for (const std::string& query_token : query_tokens) {
        if (query_token.size() < 2) {
            continue;
        }
        if (std::find(alias_tokens.begin(), alias_tokens.end(), query_token)
            != alias_tokens.end()) {
            ++overlap;
        }
    }
    std::size_t meaningful_query_tokens = 0;
    for (const std::string& query_token : query_tokens) {
        if (query_token.size() >= 2) {
            ++meaningful_query_tokens;
        }
    }
    if (meaningful_query_tokens == 0 || overlap < 2) {
        return 0.0F;
    }
    const float coverage = static_cast<float>(overlap)
        / static_cast<float>(meaningful_query_tokens);
    // Token evidence is deliberately below an exact alias match, but it lets
    // a richer OCR fragment beat an isolated short family token.
    return 0.72F + coverage * 0.20F;
}

int MatchPriority(MatchType type) noexcept {
    switch (type) {
    case MatchType::ExactName: return 6;
    case MatchType::CanonicalName: return 5;
    case MatchType::ExactShortName: return 4;
    case MatchType::CanonicalShortName: return 3;
    case MatchType::FuzzyName: return 2;
    case MatchType::FuzzyShortName: return 1;
    }
    return 0;
}

} // namespace

std::string NormalizeForMatching(std::string_view text) {
    std::wstring wide = CompatibilityNormalize(Utf8ToWide(text));
    std::wstring result;
    result.reserve(wide.size());
    bool previous_space = false;
    for (const wchar_t raw_character : wide) {
        const wchar_t character = NormalizePunctuation(raw_character);
        if (std::iswspace(character) != 0) {
            if (!result.empty() && !previous_space) {
                result.push_back(L' ');
            }
            previous_space = true;
            continue;
        }
        result.push_back(std::towlower(character));
        previous_space = false;
    }
    while (!result.empty() && result.back() == L' ') {
        result.pop_back();
    }
    return WideToUtf8(result);
}

const char* MatchTypeName(MatchType type) noexcept {
    switch (type) {
    case MatchType::ExactName: return "ExactName";
    case MatchType::ExactShortName: return "ExactShortName";
    case MatchType::CanonicalName: return "CanonicalName";
    case MatchType::CanonicalShortName: return "CanonicalShortName";
    case MatchType::FuzzyName: return "FuzzyName";
    case MatchType::FuzzyShortName: return "FuzzyShortName";
    }
    return "Unknown";
}

bool ItemCatalog::Load(const std::filesystem::path& path, std::wstring& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = L"Could not open item catalog: " + path.wstring();
        return false;
    }

    items_.clear();
    aliases_.clear();
    exact_index_.clear();
    canonical_index_.clear();
    fuzzy_index_.clear();

    std::unordered_set<std::string> ids;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.front() == '#'
            || line.rfind("id\t", 0) == 0) {
            continue;
        }
        const auto fields = SplitFields(line);
        if (fields.size() != 7 || !ValidId(fields[0]) || !ids.insert(fields[0]).second) {
            error = L"Invalid or duplicate item catalog row at line "
                + std::to_wstring(line_number);
            items_.clear();
            return false;
        }
        bool width_valid = false;
        bool height_valid = false;
        ItemRecord item{
            fields[0],
            UnescapeField(fields[1]),
            UnescapeField(fields[2]),
            UnescapeField(fields[3]),
            UnescapeField(fields[4]),
            ParseInteger(fields[5], width_valid),
            ParseInteger(fields[6], height_valid),
            {},
        };
        if (!width_valid || !height_valid || (item.nameZh.empty() && item.nameEn.empty())) {
            error = L"Invalid localized item data at line "
                + std::to_wstring(line_number);
            items_.clear();
            return false;
        }
        items_.push_back(std::move(item));
    }
    if (items_.empty()) {
        error = L"Item catalog is empty: " + path.wstring();
        return false;
    }
    BuildIndexes();
    return true;
}

void ItemCatalog::BuildIndexes() {
    for (std::size_t item_index = 0; item_index < items_.size(); ++item_index) {
        ItemRecord& item = items_[item_index];
        AddAlias(item_index, item.nameZh, Language::Zh, AliasType::Name);
        AddAlias(item_index, item.shortNameZh, Language::Zh, AliasType::ShortName);
        AddAlias(item_index, item.nameEn, Language::En, AliasType::Name);
        AddAlias(item_index, item.shortNameEn, Language::En, AliasType::ShortName);
    }
}

void ItemCatalog::AddAlias(
    std::size_t item_index,
    std::string text,
    Language language,
    AliasType type
) {
    if (text.empty()) {
        return;
    }
    const std::string normalized = NormalizeForMatching(text);
    if (normalized.empty()) {
        return;
    }
    ItemRecord& item = items_[item_index];
    const bool already_present = std::any_of(
        item.aliases.begin(),
        item.aliases.end(),
        [&](const ItemAlias& alias) {
            return alias.text == text && alias.language == language && alias.type == type;
        }
    );
    if (already_present) {
        return;
    }
    item.aliases.push_back(ItemAlias{std::move(text), language, type});
    const std::size_t item_alias_index = item.aliases.size() - 1;
    const std::size_t alias_index = aliases_.size();
    aliases_.push_back(AliasEntry{item_index, item_alias_index, normalized, type});
    exact_index_[item.aliases.back().text].push_back(alias_index);
    canonical_index_[normalized].push_back(alias_index);
    const auto code_points = CodePoints(normalized);
    if (!code_points.empty()) {
        fuzzy_index_[code_points.front()].push_back(alias_index);
    }
}

std::vector<ItemMatch> ItemCatalog::RankMatches(std::string_view text) const {
    if (text.empty()) {
        return {};
    }

    struct ScoredMatch final {
        ItemMatch match;
        int priority{};
    };
    std::unordered_map<std::size_t, ScoredMatch> best_by_item;
    const std::string input(text);
    const std::string normalized = NormalizeForMatching(text);

    const auto consider = [&](std::size_t alias_index, MatchType type, float score) {
        const AliasEntry& entry = aliases_[alias_index];
        const ItemRecord& item = items_[entry.item_index];
        const ItemAlias& alias = item.aliases[entry.item_alias_index];
        ScoredMatch candidate{
            ItemMatch{&item, alias.text, type, score},
            MatchPriority(type),
        };
        const auto found = best_by_item.find(entry.item_index);
        if (found == best_by_item.end()
            || candidate.match.score > found->second.match.score
            || (candidate.match.score == found->second.match.score
                && candidate.priority > found->second.priority)) {
            best_by_item[entry.item_index] = std::move(candidate);
        }
    };

    const auto exact = exact_index_.find(input);
    if (exact != exact_index_.end()) {
        for (const std::size_t alias_index : exact->second) {
            const AliasType type = aliases_[alias_index].type;
            consider(
                alias_index,
                type == AliasType::Name ? MatchType::ExactName : MatchType::ExactShortName,
                type == AliasType::Name ? 1.0F : 0.98F
            );
        }
    }

    if (best_by_item.empty()) {
        const auto canonical = canonical_index_.find(normalized);
        if (canonical != canonical_index_.end()) {
            for (const std::size_t alias_index : canonical->second) {
                const AliasEntry& entry = aliases_[alias_index];
                consider(
                    alias_index,
                    entry.type == AliasType::Name
                        ? MatchType::CanonicalName : MatchType::CanonicalShortName,
                    entry.type == AliasType::Name ? 0.99F : 0.97F
                );
            }
        }
    }

    if (best_by_item.empty()) {
        std::unordered_set<std::size_t> fuzzy_aliases;
        const auto query_code_points = CodePoints(normalized);
        if (!query_code_points.empty()) {
            const auto fuzzy = fuzzy_index_.find(query_code_points.front());
            if (fuzzy != fuzzy_index_.end()) {
                fuzzy_aliases.insert(fuzzy->second.begin(), fuzzy->second.end());
            }
        }

        // A richer OCR fragment can contain a family token plus a caliber or
        // model token. Include aliases containing those tokens so the richer
        // evidence is not reduced to the short alias alone.
        const auto query_tokens = EnglishTokens(normalized);
        if (query_tokens.size() >= 2) {
            for (std::size_t alias_index = 0; alias_index < aliases_.size(); ++alias_index) {
                const AliasEntry& entry = aliases_[alias_index];
                if (!HasAsciiLetter(entry.normalized)) {
                    continue;
                }
                const auto alias_tokens = EnglishTokens(entry.normalized);
                const bool shares_meaningful_token = std::any_of(
                    query_tokens.begin(),
                    query_tokens.end(),
                    [&](const std::string& query_token) {
                        return query_token.size() >= 2
                            && std::find(
                                alias_tokens.begin(), alias_tokens.end(), query_token
                            ) != alias_tokens.end();
                    }
                );
                if (shares_meaningful_token) {
                    fuzzy_aliases.insert(alias_index);
                }
            }
        }

        for (const std::size_t alias_index : fuzzy_aliases) {
            const AliasEntry& entry = aliases_[alias_index];
            const float score = std::max(
                FuzzyScore(normalized, entry.normalized),
                TokenEvidenceScore(normalized, entry.normalized)
            );
            consider(
                alias_index,
                entry.type == AliasType::Name
                    ? MatchType::FuzzyName : MatchType::FuzzyShortName,
                score
            );
        }
    }

    std::vector<ScoredMatch> ranked;
    ranked.reserve(best_by_item.size());
    for (auto& entry : best_by_item) {
        ranked.push_back(std::move(entry.second));
    }
    std::sort(ranked.begin(), ranked.end(), [](const ScoredMatch& left, const ScoredMatch& right) {
        if (left.match.score != right.match.score) {
            return left.match.score > right.match.score;
        }
        if (left.priority != right.priority) {
            return left.priority > right.priority;
        }
        return left.match.item->id < right.match.item->id;
    });

    std::vector<ItemMatch> result;
    result.reserve(ranked.size());
    if (ranked.empty()) {
        return result;
    }
    const float best_score = ranked.front().match.score;
    const float second_best_score = ranked.size() > 1
        ? ranked[1].match.score : 0.0F;
    const float score_gap = best_score - second_best_score;
    constexpr float kCompetitiveScoreGap = 0.08F;
    std::size_t competitive_count = 0;
    for (const ScoredMatch& candidate : ranked) {
        if (best_score - candidate.match.score <= kCompetitiveScoreGap) {
            ++competitive_count;
        }
    }
    const bool short_english = !ContainsCjk(normalized)
        && EnglishTokens(normalized).size() == 1
        && CodePoints(normalized).size() <= 4;
    for (ScoredMatch& candidate : ranked) {
        candidate.match.bestScore = best_score;
        candidate.match.secondBestScore = second_best_score;
        candidate.match.scoreGap = score_gap;
        candidate.match.competitiveCandidateCount = competitive_count;
        const bool full_alias = candidate.match.matchType == MatchType::ExactName
            || candidate.match.matchType == MatchType::CanonicalName;
        candidate.match.ambiguous = !full_alias
            && ((competitive_count > 1 && score_gap <= kCompetitiveScoreGap)
                || (short_english && competitive_count > 1));
        result.push_back(std::move(candidate.match));
    }
    return result;
}

std::vector<ItemMatch> ItemCatalog::MatchDiagnostics(
    std::string_view text,
    std::size_t maximum_candidates
) const {
    if (maximum_candidates == 0) {
        return {};
    }
    std::vector<ItemMatch> result = RankMatches(text);
    if (result.size() > maximum_candidates) {
        result.resize(maximum_candidates);
    }
    return result;
}

std::vector<ItemMatch> ItemCatalog::Match(
    std::string_view text,
    std::size_t maximum_candidates,
    float acceptance_threshold
) const {
    std::vector<ItemMatch> result = RankMatches(text);
    result.erase(
        std::remove_if(
            result.begin(),
            result.end(),
            [&](const ItemMatch& match) { return match.score < acceptance_threshold; }
        ),
        result.end()
    );
    if (result.size() > maximum_candidates) {
        result.resize(maximum_candidates);
    }
    return result;
}

bool ItemCatalog::IsConfidentMatch(
    std::string_view,
    const ItemMatch& match
) const noexcept {
    return match.item != nullptr && !match.ambiguous;
}

} // namespace noven::data
