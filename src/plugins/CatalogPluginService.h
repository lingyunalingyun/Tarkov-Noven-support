#pragma once
#include "plugins/CatalogData.h"
#include "data/ItemCatalog.h"
#include "data/TaskCatalog.h"
#include "data/MapCatalog.h"
#include "raid/RaidSession.h"
#include "events/EventTypes.h"
#include <array>
#include <atomic>
#include <span>
#include <memory>
#include <map>

namespace noven::plugins {
// UI 初始化时复制静态投影；工作线程只读不可变字符串，不读取 UI/游戏状态或目录指针。
// Copy static projections during UI initialization; workers read immutable strings, never UI/game state/catalog pointers.
class CatalogPluginService final {
public:
    CatalogPluginService(std::span<const data::ItemRecord> items,std::span<const data::TaskRecord> tasks,
        std::span<const data::MapRecord> maps,std::string_view locale="zh-CN");
    void SetLocale(std::string_view locale){english_.store(locale.starts_with("en"));}
    void PublishRaidHistory(std::span<const raid::RaidSession> completed);
    void PublishEvents(std::span<const events::EventRecord> events);
    DataResult Query(const DataRequest& request,const CatalogGrants& grants) const;
    static DataResult Error(const DataRequest& request,DataStatus status);
private:
    struct Record {std::string id,zh,en;};
    std::array<std::vector<Record>,3> records_;
    std::map<std::string,std::pair<std::string,std::string>,std::less<>> mapNames_;
    // 每次请求保留一个已发布投影；替换快照不使分页/查询持有的字符串失效。
    // Each request retains one published projection; replacement never invalidates its strings.
    std::atomic<std::shared_ptr<const std::vector<Record>>> history_,events_;
    std::atomic<bool> english_{};
};
}
