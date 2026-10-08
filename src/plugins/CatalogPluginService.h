#pragma once
#include "plugins/CatalogData.h"
#include "data/ItemCatalog.h"
#include "data/TaskCatalog.h"
#include "data/MapCatalog.h"
#include <array>
#include <atomic>
#include <span>

namespace noven::plugins {
// UI 初始化时复制静态投影；工作线程只读不可变字符串，不读取 UI/游戏状态或目录指针。
// Copy static projections during UI initialization; workers read immutable strings, never UI/game state/catalog pointers.
class CatalogPluginService final {
public:
    CatalogPluginService(std::span<const data::ItemRecord> items,std::span<const data::TaskRecord> tasks,
        std::span<const data::MapRecord> maps,std::string_view locale="zh-CN");
    void SetLocale(std::string_view locale){english_.store(locale.starts_with("en"));}
    DataResult Query(const DataRequest& request,const CatalogGrants& grants) const;
    static DataResult Error(const DataRequest& request,DataStatus status);
private:
    struct Record {std::string id,zh,en;};
    std::array<std::vector<Record>,3> records_;
    std::atomic<bool> english_{};
};
}
