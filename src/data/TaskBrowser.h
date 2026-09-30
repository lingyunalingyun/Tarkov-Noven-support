#pragma once

#include "data/ItemCatalog.h"
#include "data/TaskCatalog.h"

namespace noven::data {

struct TaskView final {
    const TaskRecord* source{};
    const TaskTrader* trader{};
};

class TaskBrowser final {
public:
    TaskBrowser(const TaskCatalog& tasks,const ItemCatalog& items):tasks_(tasks),items_(items){}
    [[nodiscard]] std::vector<TaskView> Query(std::string_view query,GameMode mode,std::string_view locale) const;
    [[nodiscard]] const ItemRecord* Item(std::string_view id) const { return items_.FindById(id); }
    [[nodiscard]] const TaskRecord* Task(GameMode mode,std::string_view id) const { return tasks_.Task(tasks_.StructureMode(mode),id); }
    [[nodiscard]] const TaskTrader* Trader(GameMode mode,std::string_view id) const { return tasks_.Trader(tasks_.StructureMode(mode),id); }
private:
    const TaskCatalog& tasks_;
    const ItemCatalog& items_;
};

} // namespace noven::data
