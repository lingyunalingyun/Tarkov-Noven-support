#include "scanner/SpatialCandidateSelector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace noven::scanner {

namespace {

constexpr float kPi = 3.14159265358979323846F;

int DirectionRank(const ScannerProfile& profile, ScanDirection direction) noexcept {
    for (std::size_t index = 0; index < profile.direction_priority.size(); ++index) {
        if (profile.direction_priority[index] == direction) {
            return static_cast<int>(index);
        }
    }
    return static_cast<int>(profile.direction_priority.size());
}

int RingFor(float distance, float ring_step) noexcept {
    if (ring_step <= 0.0F) {
        return 0;
    }
    return static_cast<int>(std::floor(distance / ring_step));
}

float RectangleDistanceToAnchor(
    const ocr::TextBox& box,
    AnchorPoint anchor
) noexcept {
    const float dx = anchor.x < box.x1
        ? box.x1 - anchor.x
        : (anchor.x > box.x2 ? anchor.x - box.x2 : 0.0F);
    const float dy = anchor.y < box.y1
        ? box.y1 - anchor.y
        : (anchor.y > box.y2 ? anchor.y - box.y2 : 0.0F);
    return std::hypot(dx, dy);
}

bool IsInsideTooltip(
    const ocr::TextBox& box,
    const std::optional<ocr::TextBox>& tooltip
) noexcept {
    if (!tooltip.has_value()) {
        return false;
    }
    const float center_x = (box.x1 + box.x2) / 2.0F;
    const float center_y = (box.y1 + box.y2) / 2.0F;
    return center_x >= tooltip->x1
        && center_x <= tooltip->x2
        && center_y >= tooltip->y1
        && center_y <= tooltip->y2;
}

ScanDirection ClassifyDirectionForProfile(
    float delta_x,
    float delta_y,
    float sector_half_angle_degrees
) noexcept {
    if (std::abs(sector_half_angle_degrees - 22.5F) < 0.001F) {
        return ClassifyDirection(delta_x, delta_y);
    }
    constexpr std::array<ScanDirection, 8> directions{
        ScanDirection::Right,
        ScanDirection::LowerRight,
        ScanDirection::Down,
        ScanDirection::LowerLeft,
        ScanDirection::Left,
        ScanDirection::UpperLeft,
        ScanDirection::Up,
        ScanDirection::UpperRight,
    };
    constexpr std::array<float, 8> centers{
        0.0F, 45.0F, 90.0F, 135.0F, 180.0F, -135.0F, -90.0F, -45.0F,
    };
    const float angle = std::atan2(delta_y, delta_x) * 180.0F / kPi;
    float closest_distance = std::numeric_limits<float>::max();
    ScanDirection closest = ClassifyDirection(delta_x, delta_y);
    for (std::size_t index = 0; index < directions.size(); ++index) {
        float difference = std::abs(angle - centers[index]);
        if (difference > 180.0F) {
            difference = 360.0F - difference;
        }
        if (difference < closest_distance) {
            closest_distance = difference;
            closest = directions[index];
        }
    }
    return closest_distance <= sector_half_angle_degrees
        ? closest
        : ClassifyDirection(delta_x, delta_y);
}

} // namespace

ScannerProfile InventoryProfile() noexcept {
    return ScannerProfile{
        ScanProfileType::Inventory,
        {
            ScanDirection::UpperRight,
            ScanDirection::Up,
            ScanDirection::UpperLeft,
        },
        160.0F,
        640.0F,
        0.30F,
        0.64F,
        0.22F,
        0.15F,
        0.25F,
        0.30F,
        0.08F,
        180.0F,
        true,
    };
}

ScannerProfile RaidPickupProfile() noexcept {
    return ScannerProfile{
        ScanProfileType::RaidPickup,
        {
            ScanDirection::Down,
            ScanDirection::LowerRight,
            ScanDirection::LowerLeft,
            ScanDirection::Right,
            ScanDirection::Left,
            ScanDirection::UpperRight,
            ScanDirection::UpperLeft,
            ScanDirection::Up,
        },
        160.0F,
        640.0F,
        0.30F,
        0.64F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        180.0F,
        false,
    };
}

ScannerProfile ProfileFor(ScanProfileType type) noexcept {
    return type == ScanProfileType::RaidPickup
        ? RaidPickupProfile()
        : InventoryProfile();
}

ScanDirection ClassifyDirection(float delta_x, float delta_y) noexcept {
    if (delta_x == 0.0F && delta_y == 0.0F) {
        return ScanDirection::Right;
    }

    const float angle = std::atan2(delta_y, delta_x) * 180.0F / kPi;
    if (angle >= -67.5F && angle < -22.5F) {
        return ScanDirection::UpperRight;
    }
    if (angle >= -22.5F && angle < 22.5F) {
        return ScanDirection::Right;
    }
    if (angle >= 22.5F && angle < 67.5F) {
        return ScanDirection::LowerRight;
    }
    if (angle >= 67.5F && angle < 112.5F) {
        return ScanDirection::Down;
    }
    if (angle >= 112.5F && angle < 157.5F) {
        return ScanDirection::LowerLeft;
    }
    if (angle >= 157.5F || angle < -157.5F) {
        return ScanDirection::Left;
    }
    if (angle >= -157.5F && angle < -112.5F) {
        return ScanDirection::UpperLeft;
    }
    return ScanDirection::Up;
}

const wchar_t* ScanProfileName(ScanProfileType type) noexcept {
    switch (type) {
    case ScanProfileType::Inventory:
        return L"Inventory";
    case ScanProfileType::RaidPickup:
        return L"RaidPickup";
    }
    return L"Unknown";
}

const wchar_t* ScanDirectionName(ScanDirection direction) noexcept {
    switch (direction) {
    case ScanDirection::UpperRight:
        return L"UpperRight";
    case ScanDirection::Right:
        return L"Right";
    case ScanDirection::LowerRight:
        return L"LowerRight";
    case ScanDirection::Down:
        return L"Down";
    case ScanDirection::LowerLeft:
        return L"LowerLeft";
    case ScanDirection::Left:
        return L"Left";
    case ScanDirection::UpperLeft:
        return L"UpperLeft";
    case ScanDirection::Up:
        return L"Up";
    }
    return L"Unknown";
}

const wchar_t* SpatialSearchActionName(SpatialSearchAction action) noexcept {
    switch (action) {
    case SpatialSearchAction::Continue:
        return L"continue";
    case SpatialSearchAction::Select:
        return L"select";
    }
    return L"unknown";
}

const wchar_t* ScanClassificationName(ScanClassification classification) noexcept {
    switch (classification) {
    case ScanClassification::CaptureFailed:
        return L"CAPTURE_FAILED";
    case ScanClassification::Success:
        return L"SUCCESS";
    case ScanClassification::NoText:
        return L"NO_TEXT";
    case ScanClassification::OcrFailed:
        return L"OCR_FAILED";
    case ScanClassification::NoCatalogMatch:
        return L"NO_CATALOG_MATCH";
    case ScanClassification::CatalogAmbiguous:
        return L"CATALOG_AMBIGUOUS";
    case ScanClassification::BestEffortMatch:
        return L"BEST_EFFORT_MATCH";
    case ScanClassification::OcrOnly:
        return L"OCR_ONLY";
    case ScanClassification::NoSpatialCandidate:
        return L"NO_SPATIAL_CANDIDATE";
    case ScanClassification::EconomyMissing:
        return L"ECONOMY_MISSING";
    case ScanClassification::DisplayFailed:
        return L"DISPLAY_FAILED";
    case ScanClassification::OverlayFailed:
        return L"OVERLAY_FAILED";
    }
    return L"UNKNOWN";
}

ScanResult SpatialCandidateSelector::Select(
    ScanProfileType profile_type,
    AnchorPoint anchor,
    std::span<const MatchedText> matched_texts,
    std::optional<ocr::TextBox> tooltip_region
) const {
    const ScannerProfile profile = ProfileFor(profile_type);
    struct RankedCandidate final {
        ScanCandidate candidate;
        int ring{};
        int direction_rank{};
    };
    std::vector<RankedCandidate> ranked;
    ranked.reserve(matched_texts.size());

    for (const MatchedText& matched : matched_texts) {
        if (matched.match.item == nullptr || matched.recognized.text.empty()) {
            continue;
        }

        const float center_x = (matched.recognized.box.x1 + matched.recognized.box.x2) / 2.0F;
        const float center_y = (matched.recognized.box.y1 + matched.recognized.box.y2) / 2.0F;
        const float delta_x = center_x - anchor.x;
        const float delta_y = center_y - anchor.y;
        const float center_distance = std::hypot(delta_x, delta_y);
        const float distance = RectangleDistanceToAnchor(matched.recognized.box, anchor);
        if (distance > profile.max_search_radius) {
            continue;
        }

        const ScanDirection direction = ClassifyDirectionForProfile(
            delta_x,
            delta_y,
            profile.sector_half_angle_degrees
        );
        const int direction_rank = DirectionRank(profile, direction);
        const float directional_score = 1.0F
            - static_cast<float>(direction_rank)
                / static_cast<float>(profile.direction_priority.size() - 1);
        const float spatial_score = std::max(
            0.0F,
            1.0F - distance / profile.max_search_radius
        );
        const float row_alignment_score = profile.type == ScanProfileType::Inventory
            && profile.row_alignment_tolerance > 0.0F
            ? std::exp(-std::abs(delta_y) / profile.row_alignment_tolerance)
            : 0.0F;
        const float box_width = std::max(0.0F, matched.recognized.box.x2
            - matched.recognized.box.x1);
        const float box_height = std::max(0.0F, matched.recognized.box.y2
            - matched.recognized.box.y1);
        const float ranking_score = profile.use_composite_ranking
            ? profile.match_quality_weight * matched.match.score
                + profile.ocr_confidence_weight * matched.recognized.confidence
                + profile.direction_prior_weight * directional_score
                + profile.distance_weight * spatial_score
                + profile.row_alignment_weight * row_alignment_score
            : 0.0F;
        const int ring = RingFor(distance, profile.ring_step);
        ranked.push_back(RankedCandidate{
            ScanCandidate{
                matched.recognized,
                matched.match,
                direction,
                distance,
                center_distance,
                directional_score,
                spatial_score,
                std::abs(delta_x),
                std::abs(delta_y),
                matched.recognized.box.y1 <= anchor.y
                    && anchor.y <= matched.recognized.box.y2,
                matched.recognized.box.x1 <= anchor.x
                    && anchor.x <= matched.recognized.box.x2,
                box_width,
                box_height,
                std::atan2(delta_y, delta_x) * 180.0F / kPi,
                row_alignment_score,
                spatial_score,
                ranking_score,
                ring,
                direction_rank,
                matched.sourceBoxIndices,
                matched.groupingConfidence,
                matched.grouped,
                false,
                IsInsideTooltip(matched.recognized.box, tooltip_region),
                matched.evidenceCoverage,
            },
            ring,
            direction_rank,
        });
    }

    const auto accepted = [&profile](const RankedCandidate& entry) {
        return !entry.candidate.match.ambiguous
            && entry.candidate.match.score >= profile.catalog_threshold
            && entry.candidate.recognized.confidence >= profile.minimum_ocr_confidence;
    };

    const auto ranked_before = [&profile](const RankedCandidate& left,
                                          const RankedCandidate& right) {
        const auto exact_token_priority = [](data::MatchType type) {
            if (type == data::MatchType::ExactName) return 3;
            if (type == data::MatchType::CanonicalName) return 2;
            if (type == data::MatchType::ExactShortName
                || type == data::MatchType::CanonicalShortName) return 1;
            return 0;
        };
        if (profile.type == ScanProfileType::Inventory
            && left.candidate.insideTooltip != right.candidate.insideTooltip) {
            return left.candidate.insideTooltip;
        }
        if (profile.type == ScanProfileType::Inventory
            && std::abs(
                left.candidate.evidenceCoverage - right.candidate.evidenceCoverage
            ) > 0.01F) {
            return left.candidate.evidenceCoverage
                > right.candidate.evidenceCoverage;
        }
        if (profile.type == ScanProfileType::Inventory
            && std::abs(left.candidate.match.score - right.candidate.match.score)
                > 0.01F) {
            return left.candidate.match.score > right.candidate.match.score;
        }
        if (profile.type == ScanProfileType::Inventory
            && exact_token_priority(left.candidate.match.matchType)
                != exact_token_priority(right.candidate.match.matchType)) {
            return exact_token_priority(left.candidate.match.matchType)
                > exact_token_priority(right.candidate.match.matchType);
        }
        if (profile.type == ScanProfileType::Inventory
            && std::abs(
                left.candidate.recognized.confidence
                    - right.candidate.recognized.confidence
            ) > 0.01F) {
            return left.candidate.recognized.confidence
                > right.candidate.recognized.confidence;
        }
        if (profile.type == ScanProfileType::Inventory
            && left.candidate.match.scoreGap != right.candidate.match.scoreGap) {
            return left.candidate.match.scoreGap > right.candidate.match.scoreGap;
        }
        if (profile.type == ScanProfileType::Inventory
            && std::abs(
                left.candidate.distanceToAnchor - right.candidate.distanceToAnchor
            ) > 0.5F) {
            return left.candidate.distanceToAnchor < right.candidate.distanceToAnchor;
        }
        if (profile.use_composite_ranking
            && std::abs(left.candidate.rankingScore - right.candidate.rankingScore)
                > 0.001F) {
            return left.candidate.rankingScore > right.candidate.rankingScore;
        }
        if (profile.use_composite_ranking
            && left.candidate.directionRank != right.candidate.directionRank) {
            return left.candidate.directionRank < right.candidate.directionRank;
        }
        if (left.ring != right.ring) {
            return left.ring < right.ring;
        }
        if (left.direction_rank != right.direction_rank) {
            return left.direction_rank < right.direction_rank;
        }
        if (left.candidate.match.score != right.candidate.match.score) {
            return left.candidate.match.score > right.candidate.match.score;
        }
        if (left.candidate.recognized.confidence
            != right.candidate.recognized.confidence) {
            return left.candidate.recognized.confidence
                > right.candidate.recognized.confidence;
        }
        return left.candidate.distanceToAnchor < right.candidate.distanceToAnchor;
    };

    ScanResult result;
    result.profile = profile_type;
    result.tooltipRegion = tooltip_region;
    const bool has_accepted_tooltip_candidate = profile_type == ScanProfileType::Inventory
        && std::any_of(
            ranked.begin(),
            ranked.end(),
            [&](const RankedCandidate& entry) {
                return accepted(entry) && entry.candidate.insideTooltip;
            }
        );
    if (!ranked.empty()) {
        const auto nearest = std::min_element(
            ranked.begin(),
            ranked.end(),
            [&](const RankedCandidate& left, const RankedCandidate& right) {
                const bool left_accepted = accepted(left)
                    && (!has_accepted_tooltip_candidate
                        || left.candidate.insideTooltip);
                const bool right_accepted = accepted(right)
                    && (!has_accepted_tooltip_candidate
                        || right.candidate.insideTooltip);
                if (left_accepted != right_accepted) {
                    return left_accepted;
                }
                return left.candidate.distanceToAnchor < right.candidate.distanceToAnchor;
            }
        );
        if (accepted(*nearest)
            && (!has_accepted_tooltip_candidate || nearest->candidate.insideTooltip)) {
            result.nearestValid = nearest->candidate;
        }
    }

    if (profile_type == ScanProfileType::RaidPickup) {
        std::sort(ranked.begin(), ranked.end(), ranked_before);
        for (RankedCandidate& entry : ranked) {
            if (accepted(entry)) {
                entry.candidate.participatingInSearch = true;
            }
            result.considered.push_back(entry.candidate);
        }
        const auto selected = std::find_if(
            ranked.begin(),
            ranked.end(),
            [&](const RankedCandidate& entry) { return accepted(entry); }
        );
        if (selected != ranked.end()) {
            result.found = true;
            result.selected = selected->candidate;
        }
        return result;
    }

    int selected_index = -1;
    const int maximum_ring = profile.ring_step > 0.0F
        ? static_cast<int>(std::floor(
            std::max(0.0F, profile.max_search_radius - 0.001F)
                / profile.ring_step
        ))
        : 0;
    for (int ring = 0; ring <= maximum_ring && selected_index < 0; ++ring) {
        std::vector<std::size_t> ring_pool;
        for (std::size_t direction_index = 0;
             direction_index < profile.direction_priority.size();
             ++direction_index) {
            const ScanDirection direction = profile.direction_priority[direction_index];
            std::vector<std::size_t> pool;
            for (std::size_t index = 0; index < ranked.size(); ++index) {
                const RankedCandidate& entry = ranked[index];
                if (entry.ring == ring && entry.candidate.direction == direction
                    && (!has_accepted_tooltip_candidate
                        || entry.candidate.insideTooltip)) {
                    pool.push_back(index);
                }
            }

            SpatialSearchStep step;
            step.ring = ring;
            step.direction = direction;
            step.boxCount = pool.size();
            step.validCandidateCount = pool.size();
            step.ambiguousCandidateCount = static_cast<std::size_t>(std::count_if(
                pool.begin(),
                pool.end(),
                [&](std::size_t index) { return ranked[index].candidate.match.ambiguous; }
            ));
            step.acceptedCandidateCount = static_cast<std::size_t>(std::count_if(
                pool.begin(),
                pool.end(),
                [&](std::size_t index) { return accepted(ranked[index]); }
            ));
            ring_pool.insert(ring_pool.end(), pool.begin(), pool.end());
            result.searchSteps.push_back(step);
        }

        const auto accepted_in_ring = [&](std::size_t index) {
            return accepted(ranked[index]);
        };
        if (std::any_of(ring_pool.begin(), ring_pool.end(), accepted_in_ring)) {
            std::sort(ring_pool.begin(), ring_pool.end(), [&](std::size_t left, std::size_t right) {
                return ranked_before(ranked[left], ranked[right]);
            });
            const auto selected = std::find_if(
                ring_pool.begin(),
                ring_pool.end(),
                accepted_in_ring
            );
            if (selected != ring_pool.end()) {
                selected_index = static_cast<int>(*selected);
                for (const std::size_t index : ring_pool) {
                    ranked[index].candidate.participatingInSearch = true;
                }
                for (SpatialSearchStep& step : result.searchSteps) {
                    if (step.ring == ring
                        && step.direction == ranked[*selected].candidate.direction) {
                        step.action = SpatialSearchAction::Select;
                        break;
                    }
                }
            }
        }
    }

    result.considered.reserve(ranked.size());
    for (const RankedCandidate& entry : ranked) {
        result.considered.push_back(entry.candidate);
    }
    if (selected_index >= 0) {
        result.found = true;
        result.selected = ranked[static_cast<std::size_t>(selected_index)].candidate;
    }
    return result;
}

} // namespace noven::scanner
