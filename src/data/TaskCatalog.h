#pragma once

#include "data/GameMode.h"

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace noven::data {

struct TaskTrader { std::string mode, id, nameZh, nameEn, imageKey; };
struct TaskObjective {
    std::string id, type, descriptionZh, descriptionEn;
    double count{};
    bool optional{};
    std::vector<std::string> itemIds;
};
struct TaskReward { std::string type, targetId; double value{}; };
struct TaskRecord {
    std::string mode, id, traderId, nameZh, nameEn, locationZh, locationEn, faction;
    std::int64_t minimumLevel{}, experience{};
    bool kappaRequired{}, lightkeeperRequired{};
    std::vector<std::string> prerequisites;
    std::vector<TaskObjective> objectives;
    std::vector<TaskReward> rewards;
};

// 任务结构由开发期生成资产拥有；运行时加载一次并以稳定 ID 关联。
// Generated assets own task structure; runtime loads once and joins by stable ID.
class TaskCatalog final {
public:
    bool Load(const std::filesystem::path& directory, std::wstring& error);
    [[nodiscard]] std::string_view StructureMode(GameMode mode) const;
    [[nodiscard]] const std::vector<TaskTrader>& Traders() const { return traders_; }
    [[nodiscard]] const std::vector<TaskRecord>& Tasks() const { return tasks_; }
    [[nodiscard]] const TaskTrader* Trader(std::string_view mode, std::string_view id) const;
    [[nodiscard]] const TaskRecord* Task(std::string_view mode, std::string_view id) const;
    [[nodiscard]] bool Ready() const { return !tasks_.empty(); }

private:
    std::vector<TaskTrader> traders_;
    std::vector<TaskRecord> tasks_;
};

} // namespace noven::data
