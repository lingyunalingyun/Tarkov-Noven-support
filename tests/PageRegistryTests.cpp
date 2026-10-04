#include "ui/BuiltinPages.h"
#include <cstdlib>
#include <iostream>
using namespace noven::ui;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int main(){
    auto registry=MakeBuiltinPageRegistry();
    Check(registry.Pages().size()==10&&registry.Contains(BuiltinPageId::Scanner),"all built-ins and default");
    const auto saved=registry.Find(BuiltinPageId::Settings);
    Check(saved&&saved->extensionPolicy==UiExtensionPolicy::Protected,"protected settings metadata");
    Check(registry.Find(BuiltinPageId::Map)->extensionPolicy==UiExtensionPolicy::Extensible,"ordinary page policy");
    const auto revision=registry.Revision();
    Check(!registry.Register(*saved)&&registry.Revision()==revision,"duplicates never replace");
    Check(!registry.Find(PageId{"missing.page"})&&!registry.Unregister(PageId{"missing.page"}),"unknown lookup/removal");
    for(int i=0;i<200;++i)Check(registry.Register({PageId{"plugin.test.page-"+std::to_string(i)},PageSection::Secondary,
        "nav.events","desc.events",PageIcon::GenericPlugin,20,PageSource::Plugin}),"future identity without enum changes");
    Check(registry.Find(saved->id)->titleKey==saved->titleKey,"lookup survives growth");
    const auto pages=registry.Pages();
    for(std::size_t i=1;i<pages.size();++i) {
        const auto& a=pages[i-1];const auto& b=pages[i];
        Check(a.section<b.section||(a.section==b.section&&(a.order<b.order||(a.order==b.order&&a.id<b.id))),"deterministic total ordering");
    }
    Check(registry.Pages(PageSection::Primary).size()==5,"section query");
    Check(registry.Unregister(PageId{"plugin.test.page-0"})&&!registry.Contains(PageId{"plugin.test.page-0"}),"explicit unregister");
    Check(!registry.Register({PageId{"not localized name"},PageSection::Primary,"nav.events"}),"invalid identity rejected");
    std::cout<<"Page registry identity/order/policy PASS\n";
}
