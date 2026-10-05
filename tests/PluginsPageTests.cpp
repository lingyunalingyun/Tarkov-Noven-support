#include "ui/PluginsPage.h"
#include "ui/BuiltinPages.h"
#include "ui/localization/LocalizationService.h"
#include <iostream>
#include <stdexcept>
using namespace noven::ui;
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==2,"locale argument");std::wstring error;Check(UiLocalization().DiscoverLocales(argv[1],error),"localization");
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()))),"text factory");
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;Check(SUCCEEDED(factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,16,L"en-US",&format)),"text format");
    for(const auto locale:{"zh-CN","en-US"}) {
        UiLocalization().SetLocale(locale);PluginsPage page;UiTheme theme;
        page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(!page.EmptyText().empty()&&page.Rows().empty(),"healthy empty presentation");
        const auto manifest=ParseManifest(R"({"manifestVersion":1,"id":"com.example.loot-route","name":"Loot Route","version":"1.0.0","apiVersion":1,"author":"Author","description":"Original author content","permissions":["ui.page.register","future.permission.read"]})");
        PluginSnapshot snapshot;snapshot.records.push_back({L"plugins/local",manifest.state,manifest.manifest,{}});
        snapshot.records.push_back({L"plugins/broken",PluginState::InvalidManifest,{},{{"plugins.diag.json",{}}}});
        auto incompatible=snapshot.records.front();incompatible.state=PluginState::IncompatibleApi;incompatible.manifest->apiVersion=2;snapshot.records.push_back(incompatible);
        auto conflict=snapshot.records.front();conflict.state=PluginState::DuplicateId;snapshot.records.push_back(conflict);
        auto registry=MakeBuiltinPageRegistry();const auto pageCount=registry.Pages().size();
        page.SetSnapshot(snapshot);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());
        Check(page.Rows().size()==4&&page.Rows()[0].title==L"Loot Route","author-provided name not translated");
        Check(page.Rows()[0].body.find(L"Author")!=std::wstring::npos&&page.Rows()[0].body.find(L"Original author content")!=std::wstring::npos,"valid metadata display");
        Check(page.Rows()[0].body.find(L"ui.page.register")!=std::wstring::npos&&page.Rows()[0].body.find(Tr("plugins.permissions"))!=std::wstring::npos,"requested permissions explicitly not grants");
        Check(page.Rows()[1].status==Tr("plugins.invalid")&&page.Rows()[1].body.find(Tr("plugins.diag.json"))!=std::wstring::npos,"localized invalid diagnostic");
        Check(page.Rows()[2].status==Tr("plugins.incompatible_api")&&page.Rows()[3].status==Tr("plugins.conflict"),"API incompatibility and duplicate presentation");
        Check(registry.Pages().size()==pageCount&&!registry.Contains(PageId{"plugin.com.example.loot-route.main"}),"untrusted manifest presentation never registers a page");
        const auto button=page.RefreshBounds();page.Down(button.left+10,button.top+10);Check(page.Up(button.left+10,button.top+10),"sole page action requests refresh");
        page.Down(button.left+10,button.top+10);Check(!page.Up(button.left-10,button.top+10),"refresh uses paired shared geometry");
        page.Down(button.left+10,button.top+10);page.CancelDrag();Check(!page.Up(button.left+10,button.top+10),"cancelled capture cannot trigger refresh");
        page.Down(button.right-6,170);Check(!page.Wheel(-120,theme.sidebarWidth+theme.contentPadding+30,300),"thumb drag owns scroll input");
        page.CancelDrag();Check(page.Wheel(-120,theme.sidebarWidth+theme.contentPadding+30,300),"capture cancellation releases thumb drag");
        Check(page.Wheel(-240,theme.sidebarWidth+theme.contentPadding+30,300)&&page.Animating(),"shared smooth scrolling");
        for(int frame=0;frame<200&&page.Animating();++frame)page.Tick(.016F);
        Check(!page.Animating()&&page.Scroll()>0,"scroll becomes idle");page.Blur();Check(!page.Animating(),"leaving page settles motion");
        page.SetSnapshot({});page.Prepare(880,760,theme,factory.Get(),format.Get(),format.Get());Check(page.Rows().empty()&&page.Scroll()==0,"refresh replaces snapshot and clamps scroll");
        PluginSnapshot failure;failure.diagnostics.push_back({"plugins.diag.root",{}});page.SetSnapshot(failure);
        Check(page.EmptyText()==Tr("plugins.diag.root"),"root failure does not masquerade as healthy empty state");
        const auto native=ParseManifest(R"({"manifestVersion":2,"id":"com.example.native","name":"Native","version":"1.0.0","apiVersion":1,"permissions":["ui.page.register"],"runtime":{"kind":"native-dll","entry":"plugin.dll"}})");
        PluginSnapshot runtimeSnapshot;runtimeSnapshot.records.push_back({L"plugins/native",native.state,native.manifest,{}});
        page.SetSnapshot(runtimeSnapshot);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());
        Check(page.Rows()[0].status==Tr("plugins.disabled")&&page.Rows()[0].body.find(Tr("plugins.native_unverified"))!=std::wstring::npos,"V2 defaults disabled and unverified");
        const auto enable=page.ControlBounds(native.manifest->id);Check(enable&&page.Rows()[0].enable,"explicit enable request available");
        // 按钮只产生第一方请求，不代表授权或进程创建。
        // Buttons emit first-party requests, never consent or process creation themselves.
        page.Down(enable->left+5,enable->top+5);page.Up(enable->left+5,enable->top+5);const auto request=page.TakeControlAction();Check(request&&request->enable&&request->id==native.manifest->id,"enable requires paired explicit user action");
        HostSnapshot host;host.pluginId=native.manifest->id;host.state=HostState::Running;page.SetRuntime({host});page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(page.Rows()[0].disable&&!page.Rows()[0].enable&&page.Rows()[0].status==Tr("plugins.running"),"running state offers disable only");
        const auto disable=page.ControlBounds(host.pluginId);page.Down(disable->left+5,disable->top+5);host.state=HostState::Crashed;page.SetRuntime({host});page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());page.Up(disable->left+5,disable->top+5);Check(!page.TakeControlAction(),"state change cannot reinterpret a pending Disable press as Enable");
        runtimeSnapshot.records[0].manifest->requestedPermissions.push_back("network.http");page.SetSnapshot(runtimeSnapshot);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(!page.ControlBounds(native.manifest->id)&&page.Rows()[0].body.find(Tr("plugins.unsupported"))!=std::wstring::npos,"unsupported requests cannot be granted or enabled");
        for(int switchCount=0;switchCount<20;++switchCount){page.SelectTab(switchCount%2?PluginCenterTab::MyPlugins:PluginCenterTab::Marketplace);page.Tick(.016F);}
        page.SelectTab(PluginCenterTab::Marketplace);Check(page.MarketplaceText()==Tr("plugins.marketplace_offline")&&!page.ControlBounds(native.manifest->id),"offline marketplace has no catalog or execution controls");
        page.Down(button.left+10,button.top+10);Check(!page.Up(button.left+10,button.top+10),"marketplace does not offer discovery refresh");page.Blur();Check(!page.Animating(),"leaving plugin center settles native tab motion");
    }
    std::cout<<"Read-only Plugins presentation and shared interactions PASS\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
