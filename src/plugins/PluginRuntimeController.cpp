#include "plugins/PluginRuntimeController.h"
#include "plugins/PluginNativePath.h"

namespace noven::plugins {
PluginRuntimeController::PluginRuntimeController(PluginDiscovery& discovery,PluginRuntimeManager& runtime,std::filesystem::path path)
    :discovery_(discovery),runtime_(runtime),statePath_(std::move(path)),state_(PluginStateStore::Load(statePath_)){}
const PluginRecord* PluginRuntimeController::Find(std::string_view id) const {
    for(const auto& record:discovery_.Snapshot().records)if(record.state==PluginState::Valid&&record.manifest&&record.manifest->id==id)return &record;
    return nullptr;
}
ControlResult PluginRuntimeController::Enable(std::string_view id,const std::function<bool(const PluginManifest&)>& confirm) {
    discovery_.Refresh();Reconcile();
    const auto* found=Find(id);
    if(!found||found->manifest->manifestVersion!=2||!found->manifest->runtime||!SupportedPermissions(*found->manifest))return ControlResult::Rejected;
    const auto record=*found;
    if(const auto session=runtime_.Snapshot(id);session&&!Terminal(session->state))return ControlResult::Rejected;
    std::optional<NativeFile> file;
    try{file=NativeFile::Open(discovery_.Root(),record.directory,record.manifest->runtime->entry);}
    catch(const std::exception&){return ControlResult::Rejected;}
    // 持有文件锁覆盖确认窗口；用户只确认当前已校验的清单，不能在确认中替换 DLL。
    // Retain path locks across consent; approval applies to the validated manifest/DLL, not a replacement.
    if(!confirm||!confirm(*record.manifest))return ControlResult::ConsentDeclined;
    auto candidate=state_;
    if(!candidate.Consent(*record.manifest))return ControlResult::Rejected;
    if(!candidate.Save(statePath_))return ControlResult::StateFailure;
    state_=std::move(candidate);
    if(runtime_.StartNative(record,state_))return ControlResult::Success;
    state_.Disable(id);savePending_=!state_.Save(statePath_);return savePending_?ControlResult::StateFailure:ControlResult::StartFailure;
}
ControlResult PluginRuntimeController::Disable(std::string_view id) {
    runtime_.Stop(id);state_.Disable(id);
    savePending_=!state_.Save(statePath_);return savePending_?ControlResult::StateFailure:ControlResult::Success;
}
void PluginRuntimeController::StartEnabled() {
    if(startupDone_)return;startupDone_=true;
    for(const auto& record:discovery_.Snapshot().records){
        if(!record.manifest||!state_.Intent(record.manifest->id).enabled)continue;
        if(record.state!=PluginState::Valid||!state_.Authorized(*record.manifest)||!runtime_.StartNative(record,state_)){
            state_.Disable(record.manifest->id);savePending_=!state_.Save(statePath_);
        }
    }
}
const PluginSnapshot& PluginRuntimeController::Refresh(){discovery_.Refresh();Reconcile();return discovery_.Snapshot();}
bool PluginRuntimeController::Reconcile() {
    bool changed=false;
    for(const auto& session:runtime_.Snapshots()){
        if(!state_.Intent(session.pluginId).enabled)continue;
        const auto* record=Find(session.pluginId);
        if(Terminal(session.state)||!record||!state_.Authorized(*record->manifest)){
            runtime_.Stop(session.pluginId);state_.Disable(session.pluginId);changed=true;
        }
    }
    if(changed||savePending_)savePending_=!state_.Save(statePath_);
    return !savePending_;
}
}
