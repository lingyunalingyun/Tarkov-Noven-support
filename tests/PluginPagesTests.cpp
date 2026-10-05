#include "ui/PluginPages.h"
#include "ui/BuiltinPages.h"
#include "ui/SidebarLayout.h"
#include "ui/NavigationState.h"
#include <cstdlib>
#include <iostream>
using namespace noven::ui;
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
int main(){
    auto registry=MakeBuiltinPageRegistry();PluginPages pages(registry);NavigationState navigation(registry);
    HostSnapshot session;session.pluginId="com.example.hello";session.generation=1;session.state=HostState::Running;
    session.pages.push_back({"dashboard","Hello",ParseUiDocument(R"({"schemaVersion":1,"blocks":[{"type":"button","id":"refresh","label":"Refresh"}]})")});
    pages.Sync({session});const PageId id{"plugin.com.example.hello.dashboard"};
    Check(pages.Size()==1&&registry.Contains(id)&&navigation.Select(id),"runtime page needs no enum modification");
    const auto descriptor=*registry.Find(id);Check(descriptor.source==PageSource::Plugin&&descriptor.section==PageSection::Secondary&&descriptor.extensionPolicy==UiExtensionPolicy::Extensible&&descriptor.displayTitle=="Hello","host-owned metadata placement and author title");
    for(float height:{720.0F,760.0F,1080.0F}){const auto layout=BuildSidebarLayout(registry,height,UiTheme{});const auto row=layout.Find(id);Check(row&&row->visible&&layout.HitTest(30,(row->rect.top+row->rect.bottom)/2)==id,"sample page visible with shared hit geometry at normal supported heights");Check(layout.secondaryDivider->top>=layout.Find(BuiltinPageId::Map)->rect.bottom,"section labels/divider never overlap primary pages");}
    const auto ordered=registry.Pages();Check(ordered[9].id==id&&ordered[10].id==BuiltinPageId::Plugins&&ordered[11].id==BuiltinPageId::Settings,"after secondary before protected management");
    Check(pages.Find(id)->page.document.HasAction("refresh"),"validated current document available");
    session.generation=2;session.pages[0].title="Updated";pages.Sync({session});Check(registry.Find(id)->displayTitle=="Updated"&&navigation.Active()==id,"re-enable title refresh preserves stable page identity");
    session.pages.push_back({"builtin.plugins","Spoof",{}});pages.Sync({session});Check(pages.Size()==1&&registry.Find(BuiltinPageId::Plugins)->extensionPolicy==UiExtensionPolicy::Protected,"global target spoof rejected");
    session.state=HostState::Crashed;pages.Sync({session});Check(pages.Size()==0&&!registry.Contains(id)&&navigation.Active()==BuiltinPageId::Scanner,"crash cleanup leaves no dangling identity");
    auto protectedPlugin=descriptor;protectedPlugin.extensionPolicy=UiExtensionPolicy::Protected;Check(!registry.Register(protectedPlugin),"plugin metadata cannot be protected");
    session.state=HostState::Loading;pages.Sync({session});Check(pages.Size()==0,"uninitialized session never exposes pages");
    std::cout<<"Scoped plugin registry/page lifecycle PASS\n";
}
