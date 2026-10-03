#include "scanner/InventoryRecognition.h"

// OCR 只产生检索证据；严格/尽力匹配解析稳定 ID，卡片标题取自目录。
// OCR supplies search evidence; strict/best-effort matching resolves a stable ID,
// while the card title comes from the catalog.

#include <algorithm>
#include <cctype>
#include <iterator>
#include <regex>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace noven::scanner {
namespace {

bool FullNameMatch(data::MatchType type) {
    return type == data::MatchType::ExactName
        || type == data::MatchType::CanonicalName;
}

bool ExactMatch(data::MatchType type) {
    return FullNameMatch(type)
        || type == data::MatchType::ExactShortName
        || type == data::MatchType::CanonicalShortName;
}

float TokenEvidence(const std::string& normalized, const std::string& alias) {
    if (normalized.empty() || alias.empty()) return 0.0F;
    std::size_t found = 0;
    std::size_t total = 0;
    std::size_t start = 0;
    while (start < normalized.size()) {
        while (start < normalized.size() && normalized[start] == ' ') ++start;
        const std::size_t end = normalized.find(' ', start);
        const std::string token = normalized.substr(start, end - start);
        if (!token.empty()) {
            ++total;
            if (alias.find(token) != std::string::npos) ++found;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return total == 0 ? 0.0F : static_cast<float>(found) / static_cast<float>(total);
}

std::vector<std::string> EvidenceTokens(std::string_view text) {
    std::vector<std::string> tokens;
    std::string ascii;
    std::string cjk;
    const auto flush_ascii = [&]() {
        if (!ascii.empty()) {
            tokens.push_back(ascii);
            ascii.clear();
        }
    };
    const auto flush_cjk = [&]() {
        if (!cjk.empty()) {
            tokens.push_back(cjk);
            cjk.clear();
        }
    };
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte >= 0xE0 && index + 2 < text.size()) {
            flush_ascii();
            cjk.append(text.substr(index, 3));
            index += 3;
        } else if (std::isalnum(byte) != 0 || byte == '.' || byte == '-') {
            flush_cjk();
            ascii.push_back(static_cast<char>(byte));
            ++index;
        } else {
            flush_ascii();
            flush_cjk();
            ++index;
        }
    }
    flush_ascii();
    flush_cjk();
    return tokens;
}

std::string ModelKey(std::string_view token) {
    // 保留字母与数字组成的型号标识；连字符用于识别型号，但不参与键比较。
    // Preserve alphanumeric model identity; hyphens identify a model but are omitted from its key.
    std::string result;
    bool has_digit = false;
    bool has_letter = false;
    bool has_hyphen = false;
    for (unsigned char character : token) {
        if (std::isalnum(character) != 0) {
            result.push_back(static_cast<char>(character));
            has_digit = has_digit || std::isdigit(character) != 0;
            has_letter = has_letter || std::isalpha(character) != 0;
        } else if (character == '-') {
            has_hyphen = true;
        }
    }
    return has_letter && result.size() >= 3 && (has_digit || has_hyphen)
        ? result : std::string{};
}

std::vector<std::string> Calibers(std::string_view text) {
    // 完整口径是一个语义词元；边界阻止共用 x39 后缀冒充匹配。
    // A complete caliber is one semantic token; boundaries prevent a shared x39 suffix from matching.
    static const std::regex pattern(R"((^|[^a-z0-9.])([0-9]{1,2}(?:\.[0-9]{1,2})?[x/][0-9]{2,3})(?=$|[^a-z0-9.]))");
    const std::string value(text);
    std::vector<std::string> result;
    for (std::sregex_iterator it(value.begin(), value.end(), pattern), end; it != end; ++it)
        result.push_back((*it)[2].str());
    return result;
}

std::vector<std::string> ModelCodes(std::string_view text) {
    const auto calibers = Calibers(text);
    std::vector<std::string> result;
    for (const std::string& token : EvidenceTokens(text)) {
        if (std::find(calibers.begin(), calibers.end(), token) != calibers.end()) continue;
        std::string key = ModelKey(token);
        if (!key.empty()) result.push_back(std::move(key));
    }
    return result;
}

float TokenOverlap(std::string_view query, std::string_view alias) {
    const auto query_tokens = EvidenceTokens(query);
    const auto alias_tokens = EvidenceTokens(alias);
    const auto calibers = Calibers(query);
    std::size_t common = 0;
    std::size_t total = 0;
    for (const std::string& token : query_tokens) {
        if (std::find(calibers.begin(), calibers.end(), token) != calibers.end()
            || std::all_of(token.begin(), token.end(), [](unsigned char c) {
                return std::isdigit(c) != 0;
            })) continue;
        ++total;
        if (std::find(alias_tokens.begin(), alias_tokens.end(), token) != alias_tokens.end())
            ++common;
    }
    return total == 0 ? 0.0F : static_cast<float>(common) / static_cast<float>(total);
}

float NumericOverlap(std::string_view query, std::string_view alias) {
    const auto query_tokens = EvidenceTokens(query);
    const auto alias_tokens = EvidenceTokens(alias);
    std::size_t total = 0;
    std::size_t common = 0;
    for (const std::string& token : query_tokens) {
        if (token.size() < 2 || !std::all_of(token.begin(), token.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; })) continue;
        ++total;
        if (std::find(alias_tokens.begin(), alias_tokens.end(), token) != alias_tokens.end())
            ++common;
    }
    return total == 0 ? 0.0F : static_cast<float>(common) / static_cast<float>(total);
}

enum class ItemCategory {
    Unknown, AssaultRifle, Rifle, Smg, Pistol, Shotgun, SniperRifle,
    Suppressor, Scope, Magazine, Grip, Helmet, Armor, Backpack, Component,
};

const char* CategoryName(ItemCategory category) {
    switch (category) {
    case ItemCategory::AssaultRifle: return "assault_rifle";
    case ItemCategory::Rifle: return "rifle";
    case ItemCategory::Smg: return "smg";
    case ItemCategory::Pistol: return "pistol";
    case ItemCategory::Shotgun: return "shotgun";
    case ItemCategory::SniperRifle: return "sniper_rifle";
    case ItemCategory::Suppressor: return "suppressor";
    case ItemCategory::Scope: return "scope";
    case ItemCategory::Magazine: return "magazine";
    case ItemCategory::Grip: return "grip";
    case ItemCategory::Helmet: return "helmet";
    case ItemCategory::Armor: return "armor";
    case ItemCategory::Backpack: return "backpack";
    case ItemCategory::Component: return "component";
    default: return "unknown";
    }
}

ItemCategory InferCategory(std::string_view text) {
    const auto has = [&](std::string_view word) {
        return text.find(word) != std::string_view::npos;
    };
    // 配件类别先于武器类别："assault rifle magazine" 仍是弹匣。
    // Part categories precede weapons: "assault rifle magazine" is a magazine.
    if (has("消音器") || has("suppressor") || has("silencer")) return ItemCategory::Suppressor;
    if (has("弹匣") || has("magazine")) return ItemCategory::Magazine;
    if (has("握把") || has("grip")) return ItemCategory::Grip;
    if (has("瞄准镜") || has("scope") || has("sight")) return ItemCategory::Scope;
    if (has("头盔") || has("helmet")) return ItemCategory::Helmet;
    if (has("背包") || has("backpack")) return ItemCategory::Backpack;
    if (has("护甲") || has("防弹衣") || has("插板背心") || has("armor")
        || has("plate carrier")) return ItemCategory::Armor;
    if (has("receiver") || has("handguard") || has("stock") || has("barrel")
        || has("muzzle brake") || has("机匣") || has("护木") || has("枪托")
        || has("枪管") || has("制退器")) return ItemCategory::Component;
    if (has("狙击步枪") || has("sniper rifle")) return ItemCategory::SniperRifle;
    if (has("突击步枪") || has("assault rifle")) return ItemCategory::AssaultRifle;
    if (has("冲锋枪") || has("submachine gun") || has(" smg")) return ItemCategory::Smg;
    if (has("霰弹枪") || has("shotgun")) return ItemCategory::Shotgun;
    if (has("步枪") || has(" rifle")) return ItemCategory::Rifle;
    if (has("手枪") || has("pistol")) return ItemCategory::Pistol;
    return ItemCategory::Unknown;
}

ItemCategory CatalogCategory(const data::ItemRecord& item) {
    const ItemCategory chinese = InferCategory(data::NormalizeForMatching(item.nameZh));
    return chinese == ItemCategory::Unknown
        ? InferCategory(data::NormalizeForMatching(item.nameEn)) : chinese;
}

bool Compatible(ItemCategory query, ItemCategory candidate) {
    if (query == candidate) return true;
    const auto rifle = [](ItemCategory value) {
        return value == ItemCategory::Rifle || value == ItemCategory::AssaultRifle
            || value == ItemCategory::SniperRifle;
    };
    return rifle(query) && rifle(candidate);
}

} // namespace

InventoryRecognitionResult BuildInventoryRecognition(
    std::uint64_t scan_id,
    InventoryRecognitionPath source_path,
    std::optional<ocr::TextBox> tooltip_rect,
    std::vector<ocr::RecognizedText> fragments
) {
    // 只组装本地片段，保留原框与阅读顺序以便分别构造匹配假设。
    // Assemble local fragments only; retain boxes and reading order for separate hypotheses.
    InventoryRecognitionResult result;
    result.scanId = scan_id;
    result.sourcePath = source_path;
    result.tooltipRect = tooltip_rect;
    result.rawFragments = std::move(fragments);
    result.orderedFragments = AssembleLocalTextInReadingOrder(result.rawFragments);
    result.assembledText = result.orderedFragments.completeText;
    result.normalizedText = data::NormalizeForMatching(result.assembledText);
    result.ocrConfidence = result.orderedFragments.ocrConfidence;
    return result;
}

void ResolveInventoryCatalog(
    InventoryRecognitionResult& result,
    const data::ItemCatalog& catalog,
    float threshold
) {
    // 严格路径依次检索完整标题、行、局部分组和单框；拒绝原因保留给回退路径。
    // The strict path considers the full title, lines, local groups, then boxes;
    // preserve rejection reasons for the fallback.
    result.catalogAttempted = true;
    result.candidates.clear();
    result.selectedCandidate.reset();
    result.selectedItemId.clear();
    result.rejectionReason.clear();
    if (result.normalizedText.empty()) {
        result.status = InventoryOutcomeStatus::OcrEmpty;
        result.rejectionReason = "ocr_empty";
        return;
    }

    const auto add_hypothesis = [&](const std::string& text,
                                    const ocr::TextBox& box,
                                    float confidence,
                                    std::vector<std::size_t> indices,
                                    const char* stage) {
        if (data::NormalizeForMatching(text).empty()) return;
        const auto ranked = catalog.MatchDiagnostics(text, 5);
        for (const data::ItemMatch& match : ranked) {
            if (match.item == nullptr) continue;
            const float coverage = CatalogEvidenceCoverage(
                result.assembledText, match.matchedAlias);
            const float local_coverage = CatalogEvidenceCoverage(text, match.matchedAlias);
            const float token_evidence = TokenEvidence(
                data::NormalizeForMatching(text),
                data::NormalizeForMatching(match.matchedAlias));
            const bool full_title = std::string_view(stage) == "full";
            const bool line = std::string_view(stage) == "line";
            const bool strong_full_fuzzy = full_title
                && match.matchType == data::MatchType::FuzzyName
                && match.score >= 0.78F && coverage >= 0.68F
                && match.scoreGap >= 0.08F
                && match.competitiveCandidateCount <= 1;
            const bool accepted = !match.ambiguous
                && catalog.IsConfidentMatch(text, match)
                && match.score >= threshold
                && (!full_title || ExactMatch(match.matchType) || strong_full_fuzzy)
                && (!line || ExactMatch(match.matchType)
                    || (local_coverage >= 0.68F && match.score >= 0.78F));
            std::string reason;
            if (!accepted) {
                reason = match.ambiguous ? "ambiguous"
                    : (match.score < threshold ? "low_similarity"
                        : (coverage < 0.68F && full_title ? "low_coverage"
                            : "insufficient_evidence"));
            }
            result.candidates.push_back(InventoryCatalogCandidate{
                MatchedText{
                    ocr::RecognizedText{box, text, confidence},
                    match,
                    std::move(indices),
                    1.0F,
                    !full_title && std::string_view(stage) != "fragment",
                    coverage,
                },
                stage,
                token_evidence,
                accepted,
                std::move(reason),
            });
            // Keep the original indices for other ranked aliases of this text.
            indices = result.candidates.back().text.sourceBoxIndices;
        }
    };

    const auto& assembly = result.orderedFragments;
    add_hypothesis(result.assembledText, assembly.combinedBox,
                   assembly.ocrConfidence, assembly.orderedBoxIndices, "full");
    for (const OrderedTextLine& line : assembly.lines) {
        add_hypothesis(line.text, line.combinedBox, line.ocrConfidence,
                       line.boxIndices, "line");
    }
    const auto groups = BuildLocalTextGroups(result.rawFragments);
    for (const LocalTextGroup& group : groups) {
        add_hypothesis(group.combinedText, group.combinedBox,
                       group.ocrConfidence, group.boxIndices, "group");
    }
    for (std::size_t index = 0; index < result.rawFragments.size(); ++index) {
        const auto& fragment = result.rawFragments[index];
        add_hypothesis(fragment.text, fragment.box, fragment.confidence,
                       {index}, "fragment");
    }

    const auto better = [&](std::size_t left, std::size_t right) {
        const auto& a = result.candidates[left];
        const auto& b = result.candidates[right];
        if (a.accepted != b.accepted) return a.accepted;
        const auto stage_rank = [](const InventoryCatalogCandidate& candidate) {
            if (candidate.stage == "full" && FullNameMatch(candidate.text.match.matchType)) return 5;
            if (candidate.stage == "line" && FullNameMatch(candidate.text.match.matchType)) return 4;
            if (candidate.stage == "full") return 3;
            if (candidate.stage == "group") return 2;
            if (candidate.stage == "line") return 1;
            return 0;
        };
        // 证据覆盖率与相似度分开，避免孤立短词的完美命中压过完整标题。
        // Evidence coverage is separate from similarity so a perfect stray token
        // cannot outrank a candidate explaining the complete title.
        if (a.text.evidenceCoverage != b.text.evidenceCoverage)
            return a.text.evidenceCoverage > b.text.evidenceCoverage;
        if (stage_rank(a) != stage_rank(b)) return stage_rank(a) > stage_rank(b);
        if (a.text.match.score != b.text.match.score)
            return a.text.match.score > b.text.match.score;
        if (a.tokenEvidence != b.tokenEvidence)
            return a.tokenEvidence > b.tokenEvidence;
        if (a.text.recognized.confidence != b.text.recognized.confidence)
            return a.text.recognized.confidence > b.text.recognized.confidence;
        return a.text.match.scoreGap > b.text.match.scoreGap;
    };
    std::vector<std::size_t> order(result.candidates.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::stable_sort(order.begin(), order.end(), better);
    if (!order.empty() && result.candidates[order.front()].accepted) {
        result.selectedCandidate = order.front();
        const auto& selected = result.candidates[*result.selectedCandidate].text;
        result.selectedItemId = selected.match.item->id;
        result.selectedAlias = selected.match.matchedAlias;
        result.similarity = selected.match.score;
        result.evidenceCoverage = selected.evidenceCoverage;
        result.ambiguityGap = selected.match.scoreGap;
        result.status = InventoryOutcomeStatus::Success;
        return;
    }
    result.status = !result.candidates.empty()
            && result.candidates[order.front()].text.match.ambiguous
        ? InventoryOutcomeStatus::CatalogAmbiguous
        : InventoryOutcomeStatus::CatalogNoMatch;
    result.rejectionReason = result.candidates.empty() ? "no_catalog_candidate"
        : result.candidates[order.front()].rejectionReason;
}

void BestEffortResolve(
    InventoryRecognitionResult& result,
    const data::ItemCatalog& catalog
) {
    // 不放宽严格阈值：健康目录中的可用 OCR 总会得到排名第一的规范物品。
    // Do not relax strict thresholds: usable OCR with a healthy catalog still gets
    // the top-ranked canonical item, with low-confidence/ambiguity metadata.
    if (result.selectedCandidate.has_value() || result.normalizedText.empty()) return;
    result.bestEffortTop.clear();
    result.bestEffortAmbiguous = false;
    // 标点、纯数值或单个拉丁字母不构成兜底身份，不能制造稳定物品 ID。
    // Punctuation, numeric-only OCR or one Latin letter cannot establish a fallback item ID.
    const bool has_name = std::any_of(result.normalizedText.begin(),
        result.normalizedText.end(), [](unsigned char character) {
            return std::isalpha(character) != 0 || character >= 0x80;
        });
    if (!has_name || result.normalizedText.size() == 1) {
        result.matchMode = InventoryMatchMode::OcrOnly;
        result.rejectionReason = "no_item_name";
        return;
    }
    if (catalog.AliasCount() == 0) {
        result.matchMode = InventoryMatchMode::OcrOnly;
        result.rejectionReason = "catalog_unavailable";
        return;
    }

    std::unordered_map<std::string, BestEffortCandidate> best_by_item;
    const auto query_calibers = Calibers(result.normalizedText);
    const auto query_models = ModelCodes(result.normalizedText);
    const ItemCategory query_category = InferCategory(result.normalizedText);
    // 完整标题启用目录全别名/字符三元组回退，以恢复前缀损坏或粘连的文字。
    // The full title uses the catalog-wide alias/character-trigram fallback to
    // recover damaged prefixes or merged OCR words.
    const auto consider = [&](std::string_view text, float confidence,
                              bool complete_title) {
        if (data::NormalizeForMatching(text).empty()) return;
        const auto matches = complete_title
            ? catalog.MatchBestEffort(text, 40)
            : catalog.MatchDiagnostics(text, 20);
        for (const data::ItemMatch& match : matches) {
            if (match.item == nullptr) continue;
            const float coverage = CatalogEvidenceCoverage(
                result.assembledText, match.matchedAlias);
            const std::string normalized_alias = data::NormalizeForMatching(
                match.matchedAlias);
            const std::string item_text = normalized_alias + ' '
                + data::NormalizeForMatching(match.item->nameZh) + ' '
                + data::NormalizeForMatching(match.item->nameEn);
            const auto candidate_calibers = Calibers(item_text);
            const auto candidate_models = ModelCodes(item_text);
            const ItemCategory candidate_category = CatalogCategory(*match.item);
            const bool caliber_match = std::any_of(query_calibers.begin(), query_calibers.end(),
                [&](const std::string& value) {
                    return std::find(candidate_calibers.begin(), candidate_calibers.end(), value)
                        != candidate_calibers.end();
                });
            const bool model_match = std::any_of(query_models.begin(), query_models.end(),
                [&](const std::string& value) {
                    return std::find(candidate_models.begin(), candidate_models.end(), value)
                        != candidate_models.end();
                });
            const bool model_continuation = std::any_of(query_models.begin(), query_models.end(),
                [&](const std::string& query_model) {
                    return std::any_of(candidate_models.begin(), candidate_models.end(),
                        [&](const std::string& candidate_model) {
                            return candidate_model.size() >= 5
                                && query_model.find(candidate_model) != std::string::npos;
                        });
                });
            const bool category_match = query_category != ItemCategory::Unknown
                && candidate_category != ItemCategory::Unknown
                && Compatible(query_category, candidate_category);
            const bool caliber_conflict = !query_calibers.empty()
                && !candidate_calibers.empty() && !caliber_match;
            const bool model_conflict = !query_models.empty()
                && !candidate_models.empty() && !model_match && !model_continuation;
            const bool category_conflict = query_category != ItemCategory::Unknown
                && candidate_category != ItemCategory::Unknown && !category_match;
            const float token_evidence = TokenOverlap(result.normalizedText, item_text);
            const float numeric_evidence = NumericOverlap(result.normalizedText, item_text);
            const float caliber_evidence = caliber_match ? 1.0F : 0.0F;
            const float model_evidence = model_match ? 1.0F
                : (model_continuation ? 0.5F : 0.0F);
            const float category_evidence = category_match ? 1.0F : 0.0F;
            // 完整口径、型号和粗类别分开加分；明显矛盾则扣分而非替换 OCR 原文。
            // Full caliber, model, and coarse category contribute separately;
            // contradictions penalize rank without turning OCR into a display title.
            const float penalty = (caliber_conflict ? 0.55F : 0.0F)
                + (category_conflict ? 0.50F : 0.0F)
                + (model_conflict ? 0.15F : 0.0F);
            const float score = 0.33F * match.score + 0.15F * coverage
                + 0.10F * token_evidence + 0.03F * numeric_evidence
                + 0.18F * model_evidence + 0.17F * caliber_evidence
                + 0.12F * category_evidence + 0.02F * confidence - penalty;
            BestEffortCandidate candidate{
                match, std::string(text), score, match.score, coverage,
                token_evidence, confidence,
                numeric_evidence, caliber_evidence, model_evidence,
                category_evidence, penalty,
                query_calibers.empty() ? "" : query_calibers.front(),
                candidate_calibers.empty() ? "" : candidate_calibers.front(),
                CategoryName(query_category), CategoryName(candidate_category),
                caliber_conflict, category_conflict, model_conflict,
            };
            auto found = best_by_item.find(match.item->id);
            if (found == best_by_item.end() || candidate.score > found->second.score) {
                best_by_item[match.item->id] = std::move(candidate);
            }
        }
    };
    consider(result.assembledText, result.ocrConfidence, true);
    for (const OrderedTextLine& line : result.orderedFragments.lines)
        consider(line.text, line.ocrConfidence, false);
    for (const LocalTextGroup& group : BuildLocalTextGroups(result.rawFragments))
        consider(group.combinedText, group.ocrConfidence, false);
    for (const ocr::RecognizedText& fragment : result.rawFragments)
        consider(fragment.text, fragment.confidence, false);

    for (auto& [item_id, candidate] : best_by_item)
        result.bestEffortTop.push_back(std::move(candidate));
    std::sort(result.bestEffortTop.begin(), result.bestEffortTop.end(),
        [](const BestEffortCandidate& left, const BestEffortCandidate& right) {
            if (left.score != right.score) return left.score > right.score;
            return left.match.item->id < right.match.item->id;
        });
    if (result.bestEffortTop.empty()) {
        result.matchMode = InventoryMatchMode::OcrOnly;
        result.rejectionReason = "catalog_search_failed";
        return;
    }
    const float score_gap = result.bestEffortTop.size() > 1
        ? result.bestEffortTop[0].score - result.bestEffortTop[1].score : 0.0F;
    result.bestEffortAmbiguous = result.bestEffortTop.size() > 1
        && score_gap <= 0.05F;
    if (result.bestEffortTop.size() > 3) result.bestEffortTop.resize(3);
    const BestEffortCandidate& top = result.bestEffortTop.front();
    result.matchMode = InventoryMatchMode::BestEffort;
    result.selectedItemId = top.match.item->id;
    result.selectedAlias = top.match.matchedAlias;
    result.similarity = top.similarity;
    result.evidenceCoverage = top.coverage;
    result.ambiguityGap = score_gap;
    result.candidates.push_back(InventoryCatalogCandidate{
        MatchedText{
            ocr::RecognizedText{
                result.orderedFragments.combinedBox,
                result.assembledText,
                result.ocrConfidence,
            },
            top.match,
            result.orderedFragments.orderedBoxIndices,
            1.0F,
            true,
            top.coverage,
        },
        "best_effort",
        top.tokenEvidence,
        false,
        result.rejectionReason,
    });
    result.selectedCandidate = result.candidates.size() - 1;
    result.status = InventoryOutcomeStatus::Success;
}

InventoryEconomyResolution ResolveInventoryEconomy(
    const InventoryRecognitionResult& recognition,
    const data::ItemEconomyStore& store,
    data::GameMode mode
) {
    InventoryEconomyResolution result;
    result.itemId = recognition.selectedItemId;
    if (result.itemId.empty()) return result;
    result.info = store.Lookup(mode, result.itemId);
    result.found = result.info != nullptr;
    return result;
}

std::optional<overlay::ScanDisplayResult> BuildInventoryDisplayResult(
    const InventoryRecognitionResult& recognition,
    const InventoryEconomyResolution& economy,
    data::GameMode mode
) {
    overlay::ScanDisplayResult result;
    result.assembledOcrText = recognition.assembledText;
    for (const auto& fragment : recognition.rawFragments) {
        if (!result.rawOcrText.empty()) result.rawOcrText += '\n';
        result.rawOcrText += fragment.text;
    }
    result.bestEffortAmbiguous = recognition.bestEffortAmbiguous;
    // 一旦有稳定 ID，无论置信度如何，目录规范名称都拥有结果标题。
    // Once resolved, the stable ID owns the canonical card title regardless of confidence.
    if (recognition.selectedCandidate.has_value()) {
        const auto& selected = recognition.candidates[*recognition.selectedCandidate].text;
        result.itemId = recognition.selectedItemId;
        result.displayName = overlay::DisplayNameForItem(*selected.match.item);
        result.mode = mode;
        result.matchQuality = recognition.matchMode == InventoryMatchMode::BestEffort
            ? overlay::MatchQuality::LowConfidence : overlay::MatchQuality::Strict;
        if (economy.info != nullptr) {
            result.fleaPrice = economy.info->fleaPrice;
            result.bestTrader = economy.info->bestTrader;
            result.valuePerSlot = economy.info->valuePerSlot;
            result.fleaStatus = economy.info->fleaStatus;
            result.width = economy.info->width;
            result.height = economy.info->height;
        }
        return result;
    }
    if (recognition.matchMode == InventoryMatchMode::OcrOnly) {
        result.displayName = recognition.assembledText;
        result.mode = mode;
        result.matchQuality = overlay::MatchQuality::OcrOnly;
        return result;
    }
    return std::nullopt;
}

} // namespace noven::scanner
