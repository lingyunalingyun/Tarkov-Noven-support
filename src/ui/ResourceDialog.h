#pragma once
#include "resources/ResourceService.h"
#include <windows.h>
#include <set>
namespace noven::ui {
struct ResourceDialogDecision final {bool download{};std::vector<std::string> ids;};
// UI 只拥有选择，不拥有安装状态；所有动作交给同一个 ResourceService。
// UI owns selection only, never installation state; all actions route to one ResourceService.
class ResourceDialogSelection final {
public:
    void Toggle(const resources::ResourceSnapshot& row,bool selected){if(selected)ids_.insert(row.record.resourceId);else ids_.erase(row.record.resourceId);}
    void All(const std::vector<resources::ResourceSnapshot>& rows){ids_.clear();for(const auto& row:rows)ids_.insert(row.record.resourceId);}
    void None(){ids_.clear();}
    bool Selected(std::string_view id) const {return ids_.contains(std::string(id));}
    std::uint64_t Total(const std::vector<resources::ResourceSnapshot>& rows) const {std::uint64_t size{};for(const auto& r:rows)if(Selected(r.record.resourceId))size+=r.record.downloadSize;return size;}
    bool UnknownSize(const std::vector<resources::ResourceSnapshot>& rows) const {for(const auto& row:rows)if(Selected(row.record.resourceId)&&!row.record.downloadSize)return true;return false;}
    ResourceDialogDecision Download(const std::vector<resources::ResourceSnapshot>& rows,bool canDownload) const;
private:std::set<std::string> ids_;
};
std::string_view ResourceStateKey(resources::ResourceState state);
std::string_view ResourceActionKey(resources::ResourceAction action);
ResourceDialogDecision ShowResourceDialog(HWND owner,resources::ResourceService& service,
    const std::filesystem::path& userRoot,bool onboarding);
}
