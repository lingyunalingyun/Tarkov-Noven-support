#pragma once
#include "raid/EftLogReader.h"
#include "raid/RaidSessionDetector.h"

namespace noven::raid {
struct RaidCheckpoint final {
    DetectorSnapshot detector;
    std::vector<LogCursor> cursors;
    std::filesystem::path sourceGroup;
};
// 单写入者租约、严格 schema 校验和原子替换；损坏文件绝不被空历史覆盖。
// Single-writer lease, strict schema validation and atomic replacement; corruption never becomes empty history.
class RaidSessionStore final {
public:
    ~RaidSessionStore();
    RaidSessionStore() = default;
    RaidSessionStore(const RaidSessionStore&) = delete;
    RaidSessionStore& operator=(const RaidSessionStore&) = delete;
    bool Load(const std::filesystem::path& file, RaidCheckpoint& checkpoint, std::string& error);
    bool Save(const RaidCheckpoint& checkpoint, std::string& error);
private:
    std::filesystem::path file_;
    HANDLE lease_{INVALID_HANDLE_VALUE};
    bool writable_{};
};
}
