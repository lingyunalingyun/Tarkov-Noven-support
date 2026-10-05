#pragma once
#include "ui/PageRegistry.h"
#include "plugins/PluginRuntimeManager.h"
#include <map>
#include <set>

namespace noven::ui {
struct PluginOwnedPage final {std::string pluginId;std::uint64_t generation{};plugins::RuntimePage page;};
// 只从认证会话快照产生页面；插件只能给局部身份，不能提供全局身份或保护策略。
// Pages originate only from authenticated snapshots; plugins supply local IDs, never global identity/protection policy.
class PluginPages final {
public:
    explicit PluginPages(PageRegistry& registry):registry_(registry){}
    void Sync(const std::vector<plugins::HostSnapshot>& snapshots){
        std::set<PageId> live;
        for(const auto& session:snapshots)if(session.state==plugins::HostState::Running&&plugins::ValidPluginId(session.pluginId)){
            for(const auto& page:session.pages){
                if(!plugins::ValidLocalId(page.localId)||!plugins::ValidUiText(page.title,256))continue;
                const PageId id{"plugin."+session.pluginId+"."+page.localId};
                if(const auto previous=pages_.find(id);previous!=pages_.end()&&previous->second.page.title!=page.title){registry_.Unregister(id);pages_.erase(previous);}
                if(!pages_.contains(id)){
                    PageDescriptor descriptor{id,PageSection::Secondary,"","",PageIcon::GenericPlugin,1000,PageSource::Plugin,UiExtensionPolicy::Extensible,page.title};
                    if(!registry_.Register(std::move(descriptor)))continue;
                }
                pages_[id]={session.pluginId,session.generation,page};live.insert(id);
            }
        }
        for(auto it=pages_.begin();it!=pages_.end();)if(!live.contains(it->first)){registry_.Unregister(it->first);it=pages_.erase(it);}else ++it;
    }
    const PluginOwnedPage* Find(const PageId& id) const {const auto found=pages_.find(id);return found==pages_.end()?nullptr:&found->second;}
    std::size_t Size() const noexcept {return pages_.size();}
private:
    PageRegistry& registry_;
    std::map<PageId,PluginOwnedPage> pages_;
};
}
