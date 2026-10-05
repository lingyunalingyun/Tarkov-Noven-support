#pragma once
#include "plugins/PluginRuntimeManager.h"

namespace noven::plugins {
enum class ControlResult {Success,Rejected,ConsentDeclined,StateFailure,StartFailure};
// 仅第一方 UI 拥有确认入口；刷新永不启动，崩溃撤销运行意图以避免重启循环。
// Only first-party UI owns consent; refresh never starts sessions, faults revoke intent to prevent restart loops.
class PluginRuntimeController final {
public:
    PluginRuntimeController(PluginDiscovery& discovery,PluginRuntimeManager& runtime,std::filesystem::path statePath);
    ControlResult Enable(std::string_view id,const std::function<bool(const PluginManifest&)>& confirm);
    ControlResult Disable(std::string_view id);
    void StartEnabled();
    const PluginSnapshot& Refresh();
    bool Reconcile();
    const PluginStateStore& State() const noexcept {return state_;}
private:
    const PluginRecord* Find(std::string_view id) const;
    PluginDiscovery& discovery_;
    PluginRuntimeManager& runtime_;
    std::filesystem::path statePath_;
    PluginStateStore state_;
    bool startupDone_{};
    bool savePending_{};
};
}
