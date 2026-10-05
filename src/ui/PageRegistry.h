#pragma once
#include <algorithm>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noven::ui {
// 页面身份拥有字符串，不依赖语言、枚举序号或描述符地址；可用于未来清单序列化。
// Own string identities independent of locale, enum ordinals or descriptor addresses.
class PageId final {
public:
    PageId() = default;
    explicit PageId(std::string_view value) : value_(value) {}
    const std::string& Value() const noexcept { return value_; }
    auto operator<=>(const PageId&) const = default;
private:
    std::string value_;
};
enum class PageSection { Primary, Secondary, Bottom };
enum class PageIcon { Scanner, Prices, Hideout, Tasks, Map, RaidHistory, Squad, Events, RecentScans, Settings, GenericPlugin, Plugins };
enum class PageSource { BuiltIn, Plugin };
enum class UiExtensionPolicy { Protected, Decoratable, Extensible, Replaceable };
struct PageDescriptor final {
    PageId id;
    PageSection section{PageSection::Primary};
    std::string titleKey, descriptionKey;
    PageIcon icon{PageIcon::GenericPlugin};
    int order{};
    PageSource source{PageSource::BuiltIn};
    UiExtensionPolicy extensionPolicy{UiExtensionPolicy::Extensible};
};
class PageRegistry final {
public:
    bool Register(PageDescriptor page) {
        const auto& id=page.id.Value();
        if(id.empty()||id.size()>256||id.find('.')==id.npos
            ||id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")!=id.npos
            ||page.titleKey.empty()||Contains(page.id))return false;
        pages_.push_back(std::move(page));
        std::sort(pages_.begin(),pages_.end(),[](const auto& a,const auto& b) {
            if(a.section!=b.section)return a.section<b.section;
            if(a.order!=b.order)return a.order<b.order;
            return a.id<b.id;
        });
        ++revision_;return true;
    }
    bool Unregister(const PageId& id) {
        const auto it=std::find_if(pages_.begin(),pages_.end(),[&](const auto& page){return page.id==id;});
        if(it==pages_.end())return false;
        pages_.erase(it);++revision_;return true;
    }
    // 查询返回元数据副本，注册/移除/排序不会使调用者保存的身份失效。
    // Queries return metadata copies; registration/removal/reordering cannot invalidate retained identities.
    std::optional<PageDescriptor> Find(const PageId& id) const {
        for(const auto& page:pages_)if(page.id==id)return page;
        return {};
    }
    bool Contains(const PageId& id) const {
        return std::any_of(pages_.begin(),pages_.end(),[&](const auto& page){return page.id==id;});
    }
    std::vector<PageDescriptor> Pages(std::optional<PageSection> section={}) const {
        if(!section)return pages_;
        std::vector<PageDescriptor> result;
        for(const auto& page:pages_)if(page.section==*section)result.push_back(page);
        return result;
    }
    std::uint64_t Revision() const noexcept {return revision_;}
private:
    std::vector<PageDescriptor> pages_;
    std::uint64_t revision_{};
};
}
