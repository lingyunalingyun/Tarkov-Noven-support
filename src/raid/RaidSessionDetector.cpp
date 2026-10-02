#include "raid/RaidSessionDetector.h"
#include <objbase.h>
#include <algorithm>
#include <stdexcept>

namespace noven::raid {
namespace {
std::string NewId() {
    GUID id{};
    if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("session identity unavailable");
    const auto* bytes = reinterpret_cast<const unsigned char*>(&id);
    constexpr char digits[] = "0123456789abcdef";
    std::string text("raid-");
    for (std::size_t i = 0; i < sizeof(id); ++i) {
        text += digits[bytes[i] >> 4]; text += digits[bytes[i] & 15];
    }
    return text;
}
}
const RaidSession* RaidSessionDetector::ActiveSession() const noexcept {
    return data_.active && !data_.active->sourceInterrupted ? &*data_.active : nullptr;
}
const RaidSession* RaidSessionDetector::FindSession(std::string_view id) const noexcept {
    if (data_.active && data_.active->localSessionId == id) return &*data_.active;
    const auto found = std::find_if(data_.completed.begin(), data_.completed.end(),
        [&](const auto& session) { return session.localSessionId == id; });
    return found == data_.completed.end() ? nullptr : &*found;
}
void RaidSessionDetector::SourceBoundary() {
    data_.preparing.reset(); data_.mode = GameMode::Unknown;
    data_.settlementSessionId.clear(); data_.replayingCompleted = false;
    if (data_.active) data_.active->sourceInterrupted = true;
    data_.state = SessionState::Idle;
}
bool RaidSessionDetector::Consume(const RaidEvent& event) {
    const auto prepare = [&]() -> RaidSession& {
        if (!data_.preparing) data_.preparing.emplace();
        data_.preparing->gameMode = data_.mode;
        data_.settlementSessionId.clear();
        if (!ActiveSession()) data_.state = SessionState::PreparingRaid;
        return *data_.preparing;
    };
    switch (event.kind) {
    case EventKind::SessionModeDetected:
        data_.mode = event.mode;
        if (data_.preparing) data_.preparing->gameMode = event.mode;
        return true;
    case EventKind::MapPresetDetected:
        prepare().mapId = event.mapId; data_.replayingCompleted = false; return true;
    case EventKind::RaidIdentityDetected: {
        const auto existing = std::find_if(data_.completed.begin(), data_.completed.end(),
            [&](const auto& session) { return !event.raidId.empty() && session.eftRaidId == event.raidId; });
        if (existing != data_.completed.end()) {
            data_.preparing.reset(); data_.replayingCompleted = true;
            data_.settlementSessionId = existing->localSessionId;
            return true;
        }
        if (ActiveSession() && data_.active->eftRaidId == event.raidId) return false;
        if (data_.preparing && data_.preparing->eftRaidId != event.raidId) data_.preparing.reset();
        auto& session = prepare(); session.eftRaidId = event.raidId;
        if (!event.mapId.empty()) session.mapId = event.mapId;
        data_.replayingCompleted = false; return true;
    }
    case EventKind::RaidStarted: {
        if (data_.replayingCompleted) return false;
        if (ActiveSession() && !data_.preparing) return false;
        // 没有上游 ID 时，来源文件身份和开始行偏移提供稳定的回放证据。
        // Without an upstream ID, source-file identity and start-line offset identify replay.
        const auto old = std::find_if(data_.completed.begin(), data_.completed.end(), [&](const auto& s) {
            return !event.source.empty() && s.startSource == event.source && s.startOffset == event.offset;
        });
        if (old != data_.completed.end()) {
            data_.preparing.reset(); data_.replayingCompleted = true;
            data_.settlementSessionId = old->localSessionId; return true;
        }
        RaidSession session = data_.preparing.value_or(RaidSession{});
        session.localSessionId = NewId(); session.gameMode = data_.mode;
        session.startedAt = event.time; session.startObserved = true;
        session.startSource = event.source; session.startOffset = event.offset;
        data_.active = std::move(session); data_.preparing.reset();
        data_.settlementSessionId.clear(); data_.state = SessionState::ActiveRaid; return true;
    }
    case EventKind::RaidEnded:
        if (!ActiveSession() || data_.replayingCompleted) {
            if (data_.preparing) { data_.preparing.reset(); data_.state = SessionState::Idle; return true; }
            return false;
        }
        if (event.time && data_.active->startedAt && *event.time < *data_.active->startedAt) return false;
        data_.active->endedAt = event.time; data_.active->endObserved = true;
        if (event.time && data_.active->startedAt)
            data_.active->duration = *event.time - *data_.active->startedAt;
        data_.settlementSessionId = data_.active->localSessionId;
        data_.completed.push_back(std::move(*data_.active)); data_.active.reset();
        data_.state = SessionState::CompletedRaid; return true;
    case EventKind::ScavEvidenceDetected: {
        // 结算只提供角色类型证据，绝不推断生还；新准备阶段切断上一局归属。
        // Settlement proves role only, never survival; new preparation ends prior settlement correlation.
        RaidSession* session = ActiveSession() ? &*data_.active : nullptr;
        if (!session && !data_.settlementSessionId.empty()) {
            const auto found = std::find_if(data_.completed.begin(), data_.completed.end(),
                [&](const auto& s) { return s.localSessionId == data_.settlementSessionId; });
            if (found != data_.completed.end()) session = &*found;
        }
        if (!session || session->raidType != RaidType::Unknown) return false;
        session->raidType = RaidType::Scav; return true;
    }
    }
    return false;
}
}
