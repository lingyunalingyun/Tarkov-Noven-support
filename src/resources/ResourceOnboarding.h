#pragma once
#include "common/AppPaths.h"
#include "resources/ResourceManifest.h"
#include <set>
namespace noven::resources {
// 选择模型不修改发现、安装或运行状态；UI 使用稳定 ID，不把标签当路径。
// Selection never mutates discovery/install/runtime state; UI uses stable IDs, never labels as paths.
class ResourceSelection final {
public:
    explicit ResourceSelection(const ResourceManifest& manifest);
    bool Select(std::string_view id,bool selected);
    void SelectAll();
    void SelectNone(){selected_.clear();}
    std::uint64_t TotalBytes() const;
    std::vector<std::string> SelectedIds() const {return {selected_.begin(),selected_.end()};}
private:
    ResourceManifest manifest_;
    std::set<std::string> selected_;
};
class ResourceOnboarding final {
public:
    explicit ResourceOnboarding(common::AppPaths paths):paths_(std::move(paths)){}
    bool ShouldShow() const;
    bool Complete();
private:
    common::AppPaths paths_;
};
}
