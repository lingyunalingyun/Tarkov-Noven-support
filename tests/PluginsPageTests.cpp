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
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        UiLocalization().SetLocale(locale);PluginsPage page;UiTheme theme;
        page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(!page.EmptyText().empty()&&page.Rows().empty(),"healthy empty presentation");
        const auto manifest=ParseManifest(R"({"manifestVersion":1,"id":"com.example.loot-route","name":"Loot Route","version":"1.0.0","apiVersion":1,"author":"Author","description":"Original author content","permissions":["ui.page.register","future.permission.read"]})");
        PluginSnapshot snapshot;snapshot.records.push_back({L"plugins/local",manifest.state,manifest.manifest,{}});
        snapshot.records.push_back({L"plugins/broken",PluginState::InvalidManifest,{},{{"plugins.diag.json",{}}}});
        auto incompatible=snapshot.records.front();incompatible.directory=L"plugins/incompatible";incompatible.state=PluginState::IncompatibleApi;incompatible.manifest->apiVersion=2;snapshot.records.push_back(incompatible);
        auto conflict=snapshot.records.front();conflict.directory=L"plugins/conflict";conflict.state=PluginState::DuplicateId;snapshot.records.push_back(conflict);
        auto registry=MakeBuiltinPageRegistry();const auto pageCount=registry.Pages().size();
        page.SetSnapshot(snapshot);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());
        Check(page.Rows().size()==4&&page.Rows()[0].title==L"Loot Route","author-provided name not translated");
        Check(page.Rows()[0].body.find(L"Author")!=std::wstring::npos&&page.Rows()[0].body.find(L"Original author content")!=std::wstring::npos,"valid metadata display");
        Check(page.TextInsideCards(),"centered shared formats cannot position metadata outside measured card bounds");
        Check(page.ContentBounds().left>page.ListBounds().right&&page.SearchBounds().bottom<page.ContentBounds().top,"plugin button list and selected detail have separate geometry below search");
        const auto choose=[&](std::size_t index){const auto row=page.RowBounds(index);page.Down(row.left+5,(row.top+row.bottom)/2);page.Up(row.left+5,(row.top+row.bottom)/2);};
        Check(page.SelectedPlugin()&&page.SelectedPlugin()->directory==L"plugins/local","first plugin selected by default");
        const auto category=page.CategoryBounds();
        Check(category.top>page.SearchBounds().bottom&&category.bottom<page.ContentBounds().top&&category.right<=page.ContentBounds().right,"category row fits between tabs/search and plugin panels");
        const auto openCategory=[&]{page.Down(category.left+5,category.top+5);page.Up(category.left+5,category.top+5);page.Tick(.3F);};
        openCategory();Check(page.CategoryOpen(),"shared category dropdown opens");
        const auto all=DropdownLayout{category}.Option(0);page.Down(all.left+5,all.top+5);page.Up(all.left+5,all.top+5);page.Tick(.2F);
        Check(!page.CategoryOpen()&&page.Rows().size()==4&&page.Snapshot().records.size()==4&&page.SelectedPlugin()->directory==L"plugins/local"&&!page.TakeControlAction(),"All category does not change discovery, selection or runtime intent");
        openCategory();choose(1);page.Tick(.2F);Check(!page.CategoryOpen()&&page.SelectedPlugin()->directory==L"plugins/local","outside click dismisses category without clicking through to plugin list");
        openCategory();Check(page.Key(VK_ESCAPE,false),"Escape dismisses category");page.Tick(.2F);Check(!page.CategoryOpen(),"category close animation settles");
        choose(1);Check(page.SelectedPlugin()->directory==L"plugins/broken"&&page.SelectedPlugin()->status==Tr("plugins.invalid"),"broken manifest has an independently selectable detail");
        choose(2);Check(page.SelectedPlugin()->directory==L"plugins/incompatible","same ID in different directories cannot select another record");
        page.SetSnapshot(snapshot);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(page.SelectedPlugin()->directory==L"plugins/incompatible","refresh preserves directory selection");
        page.SetSearch(L"AUTHOR");page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(page.Rows().size()==3&&page.Snapshot().records.size()==4,"case insensitive local author search never mutates discovery");
        page.SetSearch(L"missing");page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(page.Rows().empty()&&!page.EmptyText().empty(),"local no-results state");
        page.SetSearch({});page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());
        Check(!page.ControlBounds(manifest.manifest->id)&&!page.Rows()[0].enable&&!page.Rows()[0].disable&&page.Rows()[0].body.find(Tr("plugins.metadata_only"))!=std::wstring::npos,"V1 visibly remains metadata-only and non-runnable");
        const auto search=page.SearchBounds();page.Down(search.left+10,search.top+10);page.Up(search.left+10,search.top+10);
        Check(page.Char(L'L')&&page.Char(L'O')&&page.Char(L'O')&&page.Char(L'T'),"native search input accepts local query");
        page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(page.Rows().size()==3,"name search is case insensitive");
        page.SelectTab(PluginCenterTab::Marketplace);Check(!page.Char(L'X'),"offline marketplace search cannot request or mutate data");
        page.SelectTab(PluginCenterTab::MyPlugins);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(page.Rows().size()==3,"tab return preserves local search");
        page.SetSearch({});page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());choose(3);
        Check(page.SelectedPlugin()->status==Tr("plugins.conflict"),"duplicate record exposes its own conflict detail");
        page.SelectTab(PluginCenterTab::Marketplace);Check(!page.SelectedPlugin(),"marketplace cannot expose locally selected metadata as remote catalog");
        openCategory();Check(page.CategoryOpen()&&!page.SelectedPlugin()&&!page.TakeControlAction(),"marketplace category is offline and cannot expose local runtime actions");
        const auto offlineRow=page.RowBounds(0);page.Down(offlineRow.left+5,offlineRow.top+5);page.Up(offlineRow.left+5,offlineRow.top+5);
        page.SelectTab(PluginCenterTab::MyPlugins);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(!page.CategoryOpen()&&page.SelectedPlugin()->directory==L"plugins/conflict","tab return closes category and preserves plugin selection; offline clicks cannot change it");
        choose(0);
        Check(page.Rows()[0].body.find(L"ui.page.register")!=std::wstring::npos&&page.Rows()[0].body.find(Tr("plugins.permissions"))!=std::wstring::npos,"requested permissions explicitly not grants");
        Check(page.Rows()[1].status==Tr("plugins.invalid")&&page.Rows()[1].body.find(Tr("plugins.diag.json"))!=std::wstring::npos,"localized invalid diagnostic");
        Check(page.Rows()[2].status==Tr("plugins.incompatible_api")&&page.Rows()[3].status==Tr("plugins.conflict"),"API incompatibility and duplicate presentation");
        Check(registry.Pages().size()==pageCount&&!registry.Contains(PageId{"plugin.com.example.loot-route.main"}),"untrusted manifest presentation never registers a page");
        const auto button=page.RefreshBounds();page.Down(button.left+10,button.top+10);Check(page.Up(button.left+10,button.top+10),"sole page action requests refresh");
        page.Down(button.left+10,button.top+10);Check(!page.Up(button.left-10,button.top+10),"refresh uses paired shared geometry");
        page.Down(button.left+10,button.top+10);page.CancelDrag();Check(!page.Up(button.left+10,button.top+10),"cancelled capture cannot trigger refresh");
        auto longDetails=snapshot;longDetails.records[0].manifest->description=std::string(4000,'x');page.SetSnapshot(longDetails);page.Prepare(880,760,theme,factory.Get(),format.Get(),format.Get());
        const auto viewport=page.ContentBounds();
        page.Down(viewport.right-6,viewport.top+5);Check(!page.Wheel(-120,viewport.left+30,300),"thumb drag owns scroll input");
        page.CancelDrag();Check(page.Wheel(-120,viewport.left+30,300),"capture cancellation releases thumb drag");
        Check(page.Wheel(-240,viewport.left+30,300)&&page.Animating(),"shared smooth scrolling");
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
        auto longNative=runtimeSnapshot;longNative.records[0].manifest->description=std::string(4000,'x');page.SetSnapshot(longNative);page.Prepare(900,750,theme,factory.Get(),format.Get(),format.Get());
        const auto pinned=page.ControlBounds(native.manifest->id);const auto detail=page.DetailBounds();const auto panel=page.ContentBounds();
        Check(pinned&&pinned->top>=detail.bottom&&pinned->bottom==panel.bottom-12&&pinned->right<=panel.right,"Enable is bottom anchored in a separate visible action area at minimum window size");
        page.Wheel(-12000,detail.left+20,detail.top+20);for(int i=0;i<200&&page.Animating();++i)page.Tick(.016F);
        const auto afterScroll=page.ControlBounds(native.manifest->id);
        Check(page.Scroll()>0&&afterScroll&&afterScroll->top==pinned->top&&afterScroll->bottom==pinned->bottom,"long metadata scroll cannot move or cover Enable");
        Check(!page.Wheel(-120,pinned->left+5,pinned->top+5),"action area does not scroll detail");
        page.Down(pinned->left+5,pinned->top+5);page.Up(pinned->left+5,pinned->top+5);const auto pinnedRequest=page.TakeControlAction();
        Check(pinnedRequest&&pinnedRequest->enable&&pinnedRequest->id==native.manifest->id,"pinned Enable hit test remains usable after scrolling");
        page.Prepare(1280,900,theme,factory.Get(),format.Get(),format.Get());const auto resized=page.ControlBounds(native.manifest->id);
        Check(resized&&resized->bottom==page.ContentBounds().bottom-12&&resized->top>=page.DetailBounds().bottom,"action area follows resized panel without overlapping text");
        HostSnapshot host;host.pluginId=native.manifest->id;host.state=HostState::Running;page.SetRuntime({host});page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(page.Rows()[0].disable&&!page.Rows()[0].enable&&page.Rows()[0].status==Tr("plugins.running"),"running state offers disable only");
        const auto pinnedDisable=page.ControlBounds(host.pluginId);Check(pinnedDisable&&pinnedDisable->bottom==page.ContentBounds().bottom-12&&pinnedDisable->top>=page.DetailBounds().bottom,"Running Disable uses the same fixed footer");
        page.Down(pinnedDisable->left+5,pinnedDisable->top+5);page.Up(pinnedDisable->left+5,pinnedDisable->top+5);const auto disableRequest=page.TakeControlAction();Check(disableRequest&&!disableRequest->enable,"pinned Disable emits only the matching explicit disable request");
        Check(page.SelectedPlugin()->status==Tr("plugins.running")&&page.Snapshot().records.size()==1,"runtime updates keep selected detail without discovery mutation");
        const auto disable=page.ControlBounds(host.pluginId);page.Down(disable->left+5,disable->top+5);host.state=HostState::Crashed;page.SetRuntime({host});page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());page.Up(disable->left+5,disable->top+5);Check(!page.TakeControlAction(),"state change cannot reinterpret a pending Disable press as Enable");
        runtimeSnapshot.records[0].manifest->requestedPermissions.push_back("network.http");page.SetSnapshot(runtimeSnapshot);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(!page.ControlBounds(native.manifest->id)&&page.Rows()[0].body.find(Tr("plugins.unsupported"))!=std::wstring::npos,"unsupported requests cannot be granted or enabled");
        PluginSnapshot many;for(int i=0;i<32;++i){auto record=runtimeSnapshot.records[0];record.directory=L"plugins/item-"+std::to_wstring(i);record.manifest->id="com.example.item-"+std::to_string(i);record.manifest->name="Plugin "+std::to_string(i);many.records.push_back(std::move(record));}
        page.SetRuntime({});page.SetSnapshot(many);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());
        const auto list=page.ListBounds();const auto detailScroll=page.Scroll();
        page.Wheel(-12000,list.left+20,list.top+20);for(int i=0;i<200&&page.Animating();++i)page.Tick(.016F);
        const auto last=page.RowBounds(31);Check(last.top>=list.top&&last.bottom<=list.bottom&&page.Scroll()==detailScroll,"list overflow remains reachable without scrolling detail");
        choose(31);Check(page.SelectedPlugin()->id=="com.example.item-31"&&!page.TakeControlAction(),"list selection changes only detail, never enables a plugin");
        const auto clipped=page.RowBounds(0);page.Down(clipped.left+5,clipped.top+5);page.Up(clipped.left+5,clipped.top+5);Check(page.SelectedPlugin()->id=="com.example.item-31","off-viewport plugin buttons have no hit target");
        std::reverse(many.records.begin(),many.records.end());page.SetSnapshot(many);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(page.SelectedPlugin()->id=="com.example.item-31","refresh reorder preserves stable directory selection");
        many.records.erase(many.records.begin());page.SetSnapshot(many);page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());Check(page.SelectedPlugin()->id=="com.example.item-30","removed selection falls back to first remaining plugin");
        for(int switchCount=0;switchCount<20;++switchCount){page.SelectTab(switchCount%2?PluginCenterTab::MyPlugins:PluginCenterTab::Marketplace);page.Tick(.016F);}
        page.SelectTab(PluginCenterTab::Marketplace);Check(page.MarketplaceText()==Tr("plugins.marketplace_offline")&&!page.ControlBounds(native.manifest->id),"offline marketplace has no catalog or execution controls");
        page.Down(button.left+10,button.top+10);Check(!page.Up(button.left+10,button.top+10),"marketplace does not offer discovery refresh");page.Blur();Check(!page.Animating(),"leaving plugin center settles native tab motion");
    }
    std::cout<<"Read-only Plugins presentation and shared interactions PASS\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
