#include "data/PriceBrowser.h"

#include <algorithm>
#include <tuple>
#include <cctype>

namespace noven::data {
namespace {

struct Rank final {
    int kind{};
    std::size_t position{};
    std::string value;
};

Rank RankAlias(std::string_view query, std::string_view alias) {
    const std::string normalized = NormalizeForMatching(alias);
    if (normalized == query) return {0, 0, normalized};
    if (normalized.starts_with(query)) return {1, normalized.size(), normalized};
    if (normalized.find(query) != std::string::npos)
        return {2, normalized.find(query), normalized};
    return {3, normalized.size(), normalized};
}

} // namespace

std::vector<PriceRow> PriceBrowserModel::Query(
    std::string_view query, GameMode mode, PriceSortMode sortMode,
    bool descending, PriceTraderSide traderSide, std::size_t maximum,
    std::span<const PriceTagAlias> tagAliases, std::size_t offset, std::size_t* total) const {
    std::string nameQuery;
    std::vector<std::string> tags;
    // # 标签从名称查询分离；多标签取交集，含空格标签支持 #"Barter item"。
    // Separate # tags from name text; multiple tags intersect, quoted tags support spaces.
    for (std::size_t i = 0; i < query.size();) {
        if (query[i] != '#') { nameQuery += query[i++]; continue; }
        ++i;
        const bool quoted = i < query.size() && query[i] == '"';
        if (quoted) ++i;
        const auto start = i;
        while (i < query.size() && (quoted ? query[i] != '"'
            : query[i] != '#' && !std::isspace(static_cast<unsigned char>(query[i])))) ++i;
        const auto tag = NormalizeForMatching(query.substr(start, i - start));
        if (!tag.empty()) tags.push_back(tag);
        if (quoted && i < query.size()) ++i;
        nameQuery += ' ';
    }
    std::vector<std::vector<std::string>> allowedTypes;
    for (const auto& tag : tags) {
        std::vector<std::string> group{tag};
        for (const auto& alias : tagAliases)
            if (NormalizeForMatching(alias.label) == tag) group.push_back(NormalizeForMatching(alias.type));
        allowedTypes.push_back(std::move(group));
    }
    const std::string normalized = NormalizeForMatching(nameQuery);
    struct Candidate final {
        const ItemRecord* item{};
        Rank rank{};
        std::optional<double> sortValue;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(catalog_.ItemCount());

    // 先在内存目录中完成排序，再按稳定 ID 读取当前模式的经济快照。
    // Rank in the in-memory catalog first, then read the current economy snapshot by stable ID.
    for (const ItemRecord& item : catalog_.Items()) {
        if (!std::all_of(allowedTypes.begin(), allowedTypes.end(), [&](const auto& group) {
            return std::any_of(item.types.begin(), item.types.end(), [&](const auto& type) {
                const auto normalizedType = NormalizeForMatching(type);
                return std::any_of(group.begin(), group.end(), [&](const auto& allowed) {
                    return normalizedType == allowed;
                });
            });
        })) continue;
        Rank best{3, item.aliases.empty() ? 0 : item.aliases.front().text.size(), {}};
        if (nameQuery == item.id) {
            best = {0, 0, item.id};
        } else if (normalized.empty()) {
            best = {0, 0, item.id};
        } else {
            bool found = false;
            for (const ItemAlias& alias : item.aliases) {
                const Rank rank = RankAlias(normalized, alias.text);
                if (rank.kind < 3 && (!found || std::tie(rank.kind, rank.position, rank.value)
                    < std::tie(best.kind, best.position, best.value))) {
                    best = rank;
                    found = true;
                }
            }
            if (!found) continue;
        }
        std::optional<double> sortValue;
        {
            if (const ItemEconomyInfo* economy = economy_.Lookup(mode, item.id)) {
                if (sortMode == PriceSortMode::FleaChange) {
                    sortValue = economy->fleaChangeAmount;
                } else if (sortMode == PriceSortMode::FleaPrice) {
                    sortValue = economy->fleaPrice;
                } else if (traderSide == PriceTraderSide::Sell && economy->bestTrader) {
                    sortValue = economy->bestTrader->priceRoubles;
                }
            }
        }
        candidates.push_back({&item, std::move(best), sortValue});
    }

    std::sort(candidates.begin(), candidates.end(), [sortMode, descending](
        const Candidate& left, const Candidate& right) {
        {
            // 缺少价格的项目始终排在有价格的项目之后，不能把 Unknown 当成零价。
            // Unknown values stay after known prices; they are never treated as zero.
            if (left.sortValue.has_value() != right.sortValue.has_value())
                return left.sortValue.has_value();
            if (left.sortValue && right.sortValue && *left.sortValue != *right.sortValue)
                return descending ? *left.sortValue > *right.sortValue
                                  : *left.sortValue < *right.sortValue;
        }
        return std::tie(left.rank.kind, left.rank.position, left.rank.value, left.item->id)
            < std::tie(right.rank.kind, right.rank.position, right.rank.value, right.item->id);
    });
    // 全部匹配项先排序，再切页；总数不能被旧的结果上限截断。
    // Sort all matches before slicing; the total must not inherit the old result cap.
    if (total) *total = candidates.size();
    const auto begin = (std::min)(offset, candidates.size());
    const auto count = (std::min)(maximum, candidates.size() - begin);

    std::vector<PriceRow> result;
    result.reserve(count);
    for (std::size_t i = begin; i < begin + count; ++i) {
        const Candidate& candidate = candidates[i];
        PriceRow row{candidate.item, std::nullopt};
        if (const ItemEconomyInfo* info = economy_.Lookup(mode, candidate.item->id))
            row.economy = *info;
        result.push_back(std::move(row));
    }
    return result;
}

} // namespace noven::data
