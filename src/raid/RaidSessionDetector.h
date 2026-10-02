#pragma once
#include "raid/RaidEventParser.h"
#include <vector>

namespace noven::raid {
// 快照同时保存未完成上下文和已完成会话，供存储与读取游标原子提交。
// Snapshot includes unfinished context and completed sessions for atomic storage with read cursors.
struct DetectorSnapshot final {
    SessionState state{SessionState::Idle};
    GameMode mode{GameMode::Unknown};
    std::optional<RaidSession> preparing, active;
    std::vector<RaidSession> completed;
    std::string settlementSessionId;
    bool replayingCompleted{};
};
class RaidSessionDetector final {
public:
    bool Consume(const RaidEvent& event);
    void SourceBoundary();
    void Restore(DetectorSnapshot snapshot) { data_ = std::move(snapshot); }
    const DetectorSnapshot& Snapshot() const noexcept { return data_; }
    const RaidSession* ActiveSession() const noexcept;
    const RaidSession* FindSession(std::string_view localId) const noexcept;
    const std::vector<RaidSession>& CompletedSessions() const noexcept { return data_.completed; }
private:
    DetectorSnapshot data_;
};
}
