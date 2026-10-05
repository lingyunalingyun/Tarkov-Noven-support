#pragma once
#include "ui/BuiltinPages.h"
namespace noven::ui {
// 只保存拥有值的身份，不保存注册表元素指针；既有上下文返回栈继续由主窗口持有。
// Store owned identity, never registry element pointers; the existing contextual stack stays in MainWindowUi.
class NavigationState final {
public:
    explicit NavigationState(const PageRegistry& registry):registry_(registry){}
    const PageId& Active() const noexcept {
        if(registry_.Contains(active_))return active_;
        return registry_.Contains(BuiltinPageId::Scanner)?BuiltinPageId::Scanner:empty_;
    }
    bool Select(const PageId& id) {
        if(!registry_.Contains(id))return false;
        active_=id;return true;
    }
private:
    const PageRegistry& registry_;
    PageId active_{BuiltinPageId::Scanner},empty_;
};
}
