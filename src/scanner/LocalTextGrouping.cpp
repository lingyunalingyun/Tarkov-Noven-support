#include "scanner/LocalTextGrouping.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string_view>

namespace noven::scanner {

namespace {

struct BoxGeometry final {
    float centerX{};
    float centerY{};
    float width{};
    float height{};
};

BoxGeometry Geometry(const ocr::TextBox& box) noexcept {
    return BoxGeometry{
        (box.x1 + box.x2) / 2.0F,
        (box.y1 + box.y2) / 2.0F,
        std::max(0.0F, box.x2 - box.x1),
        std::max(0.0F, box.y2 - box.y1),
    };
}

float HorizontalGap(const ocr::TextBox& left, const ocr::TextBox& right) noexcept {
    return std::max(0.0F, std::max(left.x1, right.x1) - std::min(left.x2, right.x2));
}

float VerticalGap(const ocr::TextBox& top, const ocr::TextBox& bottom) noexcept {
    return std::max(0.0F, std::max(top.y1, bottom.y1) - std::min(top.y2, bottom.y2));
}

float HeightRatio(const BoxGeometry& left, const BoxGeometry& right) noexcept {
    if (left.height <= 0.0F || right.height <= 0.0F) {
        return std::numeric_limits<float>::infinity();
    }
    return std::max(left.height, right.height) / std::min(left.height, right.height);
}

bool SimilarTextHeight(
    const BoxGeometry& left,
    const BoxGeometry& right,
    const LocalTextGroupingProfile& profile
) noexcept {
    return HeightRatio(left, right) <= profile.maximum_height_ratio;
}

bool SameLine(
    const ocr::TextBox& left,
    const ocr::TextBox& right,
    const LocalTextGroupingProfile& profile
) noexcept {
    const BoxGeometry left_geometry = Geometry(left);
    const BoxGeometry right_geometry = Geometry(right);
    if (!SimilarTextHeight(left_geometry, right_geometry, profile)) {
        return false;
    }
    const float line_tolerance = std::max(
        8.0F,
        std::max(left_geometry.height, right_geometry.height)
            * profile.maximum_line_offset_multiplier
    );
    return std::abs(left_geometry.centerY - right_geometry.centerY) <= line_tolerance
        && HorizontalGap(left, right)
            <= std::max(left_geometry.height, right_geometry.height)
                * profile.maximum_gap_multiplier;
}

bool Stacked(
    const ocr::TextBox& left,
    const ocr::TextBox& right,
    const LocalTextGroupingProfile& profile
) noexcept {
    const BoxGeometry left_geometry = Geometry(left);
    const BoxGeometry right_geometry = Geometry(right);
    if (!SimilarTextHeight(left_geometry, right_geometry, profile)) {
        return false;
    }
    const float maximum_gap = std::max(
        12.0F,
        std::max(left_geometry.height, right_geometry.height)
            * profile.maximum_gap_multiplier
    );
    const float horizontal_overlap = std::max(
        0.0F,
        std::min(left.x2, right.x2) - std::max(left.x1, right.x1)
    );
    const float horizontal_center_gap = std::abs(left_geometry.centerX - right_geometry.centerX);
    return VerticalGap(left, right) <= maximum_gap
        && (horizontal_overlap > 0.0F || horizontal_center_gap <= maximum_gap);
}

bool Compatible(
    const ocr::RecognizedText& left,
    const ocr::RecognizedText& right,
    const LocalTextGroupingProfile& profile
) noexcept {
    return SameLine(left.box, right.box, profile)
        || Stacked(left.box, right.box, profile);
}

bool ReadingOrderLess(
    const ocr::RecognizedText& left,
    const ocr::RecognizedText& right
) noexcept {
    const BoxGeometry left_geometry = Geometry(left.box);
    const BoxGeometry right_geometry = Geometry(right.box);
    const float vertical_difference = std::abs(
        left_geometry.centerY - right_geometry.centerY
    );
    const float line_tolerance = std::max(
        8.0F,
        std::max(left_geometry.height, right_geometry.height) * 0.75F
    );
    if (vertical_difference <= line_tolerance) {
        return left_geometry.centerX < right_geometry.centerX;
    }
    if (left_geometry.centerY != right_geometry.centerY) {
        return left_geometry.centerY < right_geometry.centerY;
    }
    return left_geometry.centerX < right_geometry.centerX;
}

std::string Trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

float PairConfidence(
    const ocr::RecognizedText& left,
    const ocr::RecognizedText& right,
    const LocalTextGroupingProfile& profile
) noexcept {
    const BoxGeometry left_geometry = Geometry(left.box);
    const BoxGeometry right_geometry = Geometry(right.box);
    const float height_score = 1.0F / HeightRatio(left_geometry, right_geometry);
    const float gap = SameLine(left.box, right.box, profile)
        ? HorizontalGap(left.box, right.box)
        : VerticalGap(left.box, right.box);
    const float gap_scale = std::max(
        1.0F,
        std::max(left_geometry.height, right_geometry.height)
            * profile.maximum_gap_multiplier
    );
    const float gap_score = std::max(0.0F, 1.0F - gap / gap_scale);
    return (height_score + gap_score) / 2.0F;
}

void UpdateBounds(
    const ocr::RecognizedText& text,
    float& left,
    float& top,
    float& right,
    float& bottom,
    float& minimum_confidence
) noexcept {
    left = std::min(left, text.box.x1);
    top = std::min(top, text.box.y1);
    right = std::max(right, text.box.x2);
    bottom = std::max(bottom, text.box.y2);
    minimum_confidence = std::min(minimum_confidence, text.confidence);
}

LocalTextGroup MakeGroup(
    const std::vector<std::size_t>& indices,
    std::span<const ocr::RecognizedText> texts,
    const LocalTextGroupingProfile& profile
) {
    std::vector<std::size_t> ordered = indices;
    std::sort(ordered.begin(), ordered.end(), [&](std::size_t left, std::size_t right) {
        return ReadingOrderLess(texts[left], texts[right]);
    });

    float left = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::lowest();
    float minimum_confidence = std::numeric_limits<float>::max();
    float confidence_sum = 0.0F;
    std::string combined_text;
    for (const std::size_t index : ordered) {
        const ocr::RecognizedText& text = texts[index];
        UpdateBounds(text, left, top, right, bottom, minimum_confidence);
        confidence_sum += text.confidence;
        const std::string trimmed = Trim(text.text);
        if (!trimmed.empty()) {
            if (!combined_text.empty()) {
                combined_text.push_back(' ');
            }
            combined_text += trimmed;
        }
    }

    float pair_confidence_sum = 0.0F;
    std::size_t pair_count = 0;
    for (std::size_t left_index = 0; left_index < ordered.size(); ++left_index) {
        for (std::size_t right_index = left_index + 1;
             right_index < ordered.size();
             ++right_index) {
            pair_confidence_sum += PairConfidence(
                texts[ordered[left_index]],
                texts[ordered[right_index]],
                profile
            );
            ++pair_count;
        }
    }

    const float average_ocr_confidence = confidence_sum / static_cast<float>(ordered.size());
    return LocalTextGroup{
        std::move(ordered),
        ocr::TextBox{left, top, right, bottom, minimum_confidence},
        std::move(combined_text),
        pair_count == 0 ? 0.0F : pair_confidence_sum / static_cast<float>(pair_count),
        average_ocr_confidence,
    };
}

bool WithinBounds(
    const LocalTextGroup& group,
    const LocalTextGroupingProfile& profile
) noexcept {
    return group.combinedBox.x2 - group.combinedBox.x1 <= profile.maximum_group_width
        && group.combinedBox.y2 - group.combinedBox.y1 <= profile.maximum_group_height;
}

std::vector<char32_t> Utf8CodePoints(std::string_view text) {
    std::vector<char32_t> result;
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        std::size_t count = 1;
        char32_t value = first;
        if ((first & 0xE0U) == 0xC0U && index + 1 < text.size()) {
            count = 2;
            value = first & 0x1FU;
        } else if ((first & 0xF0U) == 0xE0U && index + 2 < text.size()) {
            count = 3;
            value = first & 0x0FU;
        } else if ((first & 0xF8U) == 0xF0U && index + 3 < text.size()) {
            count = 4;
            value = first & 0x07U;
        }
        bool valid = count > 1;
        for (std::size_t continuation = 1; continuation < count; ++continuation) {
            const unsigned char byte = static_cast<unsigned char>(text[index + continuation]);
            if ((byte & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            value = (value << 6U) | (byte & 0x3FU);
        }
        if (!valid && count > 1) {
            count = 1;
            value = first;
        }
        result.push_back(value);
        index += count;
    }
    return result;
}

bool IsCjk(char32_t value) noexcept {
    return (value >= 0x3400 && value <= 0x9FFF)
        || (value >= 0xF900 && value <= 0xFAFF);
}

bool IsOpeningPunctuation(char32_t value) noexcept {
    return value == U'(' || value == U'[' || value == U'{' || value == U'\uFF08'
        || value == U'\u3010' || value == U'\u300A';
}

bool IsClosingPunctuation(char32_t value) noexcept {
    return value == U')' || value == U']' || value == U'}' || value == U','
        || value == U'.' || value == U':' || value == U';' || value == U'\uFF09'
        || value == U'\u3001' || value == U'\u3002' || value == U'\u3011'
        || value == U'\u300B';
}

void AppendReadingFragment(std::string& output, const std::string& fragment) {
    const std::string trimmed = Trim(fragment);
    if (trimmed.empty()) {
        return;
    }
    if (!output.empty()) {
        const std::vector<char32_t> left = Utf8CodePoints(output);
        const std::vector<char32_t> right = Utf8CodePoints(trimmed);
        const bool join_without_space = !left.empty() && !right.empty()
            && (IsOpeningPunctuation(left.back())
                || IsClosingPunctuation(right.front())
                || (IsCjk(left.back()) && IsCjk(right.front())));
        if (!join_without_space) {
            output.push_back(' ');
        }
    }
    output += trimmed;
}

} // namespace

LocalTextGroupingProfile DefaultLocalTextGroupingProfile() noexcept {
    return LocalTextGroupingProfile{};
}

bool GroupedMatchClearlyBetter(float grouped_score, float single_score) noexcept {
    return grouped_score > single_score + 0.05F;
}

std::vector<TextNeighborDiagnostic> FindNearestTextNeighbors(
    std::span<const ocr::RecognizedText> texts,
    std::size_t box_index,
    const LocalTextGroupingProfile& profile,
    std::size_t maximum_neighbors
) {
    if (box_index >= texts.size() || maximum_neighbors == 0) {
        return {};
    }

    std::vector<TextNeighborDiagnostic> neighbors;
    for (std::size_t index = 0; index < texts.size(); ++index) {
        if (index == box_index) {
            continue;
        }
        const BoxGeometry current = Geometry(texts[box_index].box);
        neighbors.push_back(TextNeighborDiagnostic{
            index,
            HorizontalGap(texts[box_index].box, texts[index].box),
            VerticalGap(texts[box_index].box, texts[index].box),
            SameLine(texts[box_index].box, texts[index].box, profile),
            Stacked(texts[box_index].box, texts[index].box, profile),
        });
        std::stable_sort(neighbors.begin(), neighbors.end(), [&](const auto& left, const auto& right) {
            const BoxGeometry left_geometry = Geometry(texts[left.boxIndex].box);
            const BoxGeometry right_geometry = Geometry(texts[right.boxIndex].box);
            const float left_distance = std::hypot(
                left_geometry.centerX - current.centerX,
                left_geometry.centerY - current.centerY
            );
            const float right_distance = std::hypot(
                right_geometry.centerX - current.centerX,
                right_geometry.centerY - current.centerY
            );
            return left_distance < right_distance;
        });
        if (neighbors.size() > maximum_neighbors) {
            neighbors.resize(maximum_neighbors);
        }
    }
    return neighbors;
}

std::vector<LocalTextGroup> BuildLocalTextGroups(
    std::span<const ocr::RecognizedText> texts,
    const LocalTextGroupingProfile& profile
) {
    if (profile.maximum_boxes < 2 || texts.size() < 2) {
        return {};
    }

    std::vector<LocalTextGroup> groups;
    for (std::size_t start = 0; start < texts.size(); ++start) {
        std::vector<std::size_t> neighbors;
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (std::size_t index = start + 1; index < texts.size(); ++index) {
                if (std::find(neighbors.begin(), neighbors.end(), index) != neighbors.end()) {
                    continue;
                }
                bool compatible_with_local_group = Compatible(texts[start], texts[index], profile);
                for (const std::size_t neighbor : neighbors) {
                    if (Compatible(texts[neighbor], texts[index], profile)) {
                        compatible_with_local_group = true;
                        break;
                    }
                }
                if (compatible_with_local_group) {
                    neighbors.push_back(index);
                    expanded = true;
                }
            }
        }
        const BoxGeometry start_geometry = Geometry(texts[start].box);
        std::sort(neighbors.begin(), neighbors.end(), [&](std::size_t left, std::size_t right) {
            const BoxGeometry left_geometry = Geometry(texts[left].box);
            const BoxGeometry right_geometry = Geometry(texts[right].box);
            const float left_distance = std::hypot(
                left_geometry.centerX - start_geometry.centerX,
                left_geometry.centerY - start_geometry.centerY
            );
            const float right_distance = std::hypot(
                right_geometry.centerX - start_geometry.centerX,
                right_geometry.centerY - start_geometry.centerY
            );
            return left_distance < right_distance;
        });
        if (neighbors.size() > profile.maximum_neighbors) {
            neighbors.resize(profile.maximum_neighbors);
        }
        std::vector<std::size_t> local_indices{start};
        local_indices.insert(local_indices.end(), neighbors.begin(), neighbors.end());
        std::sort(local_indices.begin(), local_indices.end(), [&](std::size_t left, std::size_t right) {
            return ReadingOrderLess(texts[left], texts[right]);
        });
        local_indices.erase(
            std::unique(local_indices.begin(), local_indices.end()),
            local_indices.end()
        );

        for (std::size_t begin = 0; begin < local_indices.size(); ++begin) {
            for (std::size_t count = 2;
                 count <= profile.maximum_boxes
                     && begin + count <= local_indices.size();
                 ++count) {
                bool contiguous = true;
                for (std::size_t offset = 1; offset < count; ++offset) {
                    if (!Compatible(
                            texts[local_indices[begin + offset - 1]],
                            texts[local_indices[begin + offset]],
                            profile
                        )) {
                        contiguous = false;
                        break;
                    }
                }
                if (!contiguous) {
                    continue;
                }
                std::vector<std::size_t> indices(
                    local_indices.begin() + static_cast<std::ptrdiff_t>(begin),
                    local_indices.begin()
                        + static_cast<std::ptrdiff_t>(begin + count)
                );
                const LocalTextGroup group = MakeGroup(indices, texts, profile);
                if (!WithinBounds(group, profile) || group.combinedText.empty()) {
                    continue;
                }
                const auto duplicate = std::find_if(
                    groups.begin(),
                    groups.end(),
                    [&](const LocalTextGroup& existing) {
                        return existing.boxIndices == group.boxIndices;
                    }
                );
                if (duplicate == groups.end()) {
                    groups.push_back(group);
                }
            }
        }
    }
    return groups;
}

std::vector<TextGroupMatch> MatchLocalTextGroups(
    const data::ItemCatalog& catalog,
    std::span<const ocr::RecognizedText> texts,
    float acceptance_threshold,
    const LocalTextGroupingProfile& profile
) {
    std::vector<TextGroupMatch> matches;
    const std::vector<LocalTextGroup> groups = BuildLocalTextGroups(texts, profile);
    matches.reserve(groups.size());
    for (const LocalTextGroup& group : groups) {
        const std::vector<data::ItemMatch> candidates = catalog.Match(
            group.combinedText,
            1,
            acceptance_threshold
        );
        if (!candidates.empty() && !candidates.front().ambiguous) {
            matches.push_back(TextGroupMatch{
                group.boxIndices,
                group.combinedBox,
                group.combinedText,
                candidates.front(),
                group.groupingConfidence,
                group.ocrConfidence,
            });
        }
    }
    return matches;
}

OrderedTextAssembly AssembleLocalTextInReadingOrder(
    std::span<const ocr::RecognizedText> texts
) {
    OrderedTextAssembly result;
    if (texts.empty()) {
        return result;
    }

    std::vector<std::size_t> by_vertical_position(texts.size());
    std::iota(by_vertical_position.begin(), by_vertical_position.end(), 0);
    std::stable_sort(
        by_vertical_position.begin(),
        by_vertical_position.end(),
        [&](std::size_t left, std::size_t right) {
            const BoxGeometry left_geometry = Geometry(texts[left].box);
            const BoxGeometry right_geometry = Geometry(texts[right].box);
            if (left_geometry.centerY != right_geometry.centerY) {
                return left_geometry.centerY < right_geometry.centerY;
            }
            return left_geometry.centerX < right_geometry.centerX;
        }
    );

    std::vector<std::vector<std::size_t>> line_indices;
    for (const std::size_t index : by_vertical_position) {
        const BoxGeometry geometry = Geometry(texts[index].box);
        auto line = std::find_if(
            line_indices.begin(),
            line_indices.end(),
            [&](const std::vector<std::size_t>& candidate) {
                float center_sum = 0.0F;
                float maximum_height = geometry.height;
                for (const std::size_t member : candidate) {
                    const BoxGeometry member_geometry = Geometry(texts[member].box);
                    center_sum += member_geometry.centerY;
                    maximum_height = std::max(maximum_height, member_geometry.height);
                }
                const float baseline = center_sum / static_cast<float>(candidate.size());
                return std::abs(geometry.centerY - baseline)
                    <= std::max(8.0F, maximum_height * 0.65F);
            }
        );
        if (line == line_indices.end()) {
            line_indices.push_back({index});
        } else {
            line->push_back(index);
        }
    }

    std::stable_sort(
        line_indices.begin(),
        line_indices.end(),
        [&](const auto& left, const auto& right) {
            const auto line_top = [&](const std::vector<std::size_t>& line) {
                return texts[*std::min_element(
                    line.begin(), line.end(),
                    [&](std::size_t a, std::size_t b) {
                        return texts[a].box.y1 < texts[b].box.y1;
                    }
                )].box.y1;
            };
            return line_top(left) < line_top(right);
        }
    );

    float all_left = std::numeric_limits<float>::max();
    float all_top = std::numeric_limits<float>::max();
    float all_right = std::numeric_limits<float>::lowest();
    float all_bottom = std::numeric_limits<float>::lowest();
    float confidence_sum = 0.0F;
    for (std::vector<std::size_t>& line : line_indices) {
        std::stable_sort(line.begin(), line.end(), [&](std::size_t left, std::size_t right) {
            return Geometry(texts[left].box).centerX < Geometry(texts[right].box).centerX;
        });
        float left = std::numeric_limits<float>::max();
        float top = std::numeric_limits<float>::max();
        float right = std::numeric_limits<float>::lowest();
        float bottom = std::numeric_limits<float>::lowest();
        float line_confidence = 0.0F;
        std::string line_text;
        for (const std::size_t index : line) {
            result.orderedBoxIndices.push_back(index);
            AppendReadingFragment(line_text, texts[index].text);
            left = std::min(left, texts[index].box.x1);
            top = std::min(top, texts[index].box.y1);
            right = std::max(right, texts[index].box.x2);
            bottom = std::max(bottom, texts[index].box.y2);
            line_confidence += texts[index].confidence;
            confidence_sum += texts[index].confidence;
        }
        AppendReadingFragment(result.completeText, line_text);
        all_left = std::min(all_left, left);
        all_top = std::min(all_top, top);
        all_right = std::max(all_right, right);
        all_bottom = std::max(all_bottom, bottom);
        result.lines.push_back(OrderedTextLine{
            line,
            ocr::TextBox{left, top, right, bottom, 1.0F},
            std::move(line_text),
            line_confidence / static_cast<float>(line.size()),
        });
    }
    result.combinedBox = ocr::TextBox{
        all_left, all_top, all_right, all_bottom, 1.0F,
    };
    result.ocrConfidence = confidence_sum / static_cast<float>(texts.size());
    return result;
}

float CatalogEvidenceCoverage(
    const std::string& complete_local_text,
    const std::string& catalog_alias
) {
    const std::vector<char32_t> evidence = Utf8CodePoints(
        data::NormalizeForMatching(complete_local_text)
    );
    const std::vector<char32_t> alias = Utf8CodePoints(
        data::NormalizeForMatching(catalog_alias)
    );
    if (evidence.empty() || alias.empty()) {
        return 0.0F;
    }
    std::vector<std::size_t> previous(alias.size() + 1, 0);
    std::vector<std::size_t> current(alias.size() + 1, 0);
    for (const char32_t evidence_character : evidence) {
        for (std::size_t index = 1; index <= alias.size(); ++index) {
            current[index] = evidence_character == alias[index - 1]
                ? previous[index - 1] + 1
                : std::max(previous[index], current[index - 1]);
        }
        std::swap(previous, current);
        std::fill(current.begin(), current.end(), 0);
    }
    return static_cast<float>(previous.back())
        / static_cast<float>(evidence.size());
}

} // namespace noven::scanner
