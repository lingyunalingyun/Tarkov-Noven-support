#include "ui/PluginsPage.h"
#include "ui/BuiltinPages.h"
#include "ui/localization/LocalizationService.h"
#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
using namespace noven::ui;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp final {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-boundary-test-"+ipc::RandomSecret());
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==2,"locale resources required");std::wstring error;Check(UiLocalization().DiscoverLocales(argv[1],error),"localization");
    Temp temporary;PluginDiscovery discovery(temporary.path);PluginRuntimeManager runtime;
    wchar_t self[32768]{};Check(GetModuleFileNameW(nullptr,self,32768)!=0,"runtime executable path");
    Check(runtime.HostPath()==std::filesystem::path(self).parent_path()/L"NovenPluginHost.exe","production path is executable-relative, never discovery root or cwd");
    const auto local=discovery.Root()/"com.example.loot-route";std::filesystem::create_directories(local);
    {std::ofstream file(local/"manifest.json");file<<R"({"manifestVersion":1,"id":"com.example.loot-route","name":"Loot Route","version":"1.0.0","apiVersion":1,"permissions":["ui.page.register","network.http"],"entry":"evil.exe","dll":"evil.dll","exe":"evil.exe","command":"ignored","runtimePath":"../../outside"})";}
    // 伪二进制只是数据，不能被检查、加载或启动；清单未知字段保持无害。
    // Fake binary contents are data only, never inspected/loaded/launched; unknown manifest fields remain harmless.
    {std::ofstream file(local/"evil.exe");file<<"not executable";}
    {std::ofstream file(local/"evil.dll");file<<"not a library";}
    auto registry=MakeBuiltinPageRegistry();const auto count=registry.Pages().size();
    PluginsPage page;page.SetSnapshot(discovery.Refresh());
    Check(discovery.Snapshot().records.size()==1&&discovery.Snapshot().records[0].state==PluginState::Valid,"V1 discovery tolerates unknown executable-looking fields");
    Check(runtime.SessionCount()==0,"startup discovery spawns zero host sessions");
    const auto protectedPage=registry.Find(BuiltinPageId::Plugins);Check(protectedPage&&protectedPage->extensionPolicy==UiExtensionPolicy::Protected,"management remains protected");
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()))),"DWrite factory");
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;Check(SUCCEEDED(factory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,16,L"zh-CN",&format)),"DWrite format");
    UiTheme theme;page.Prepare(1280,760,theme,factory.Get(),format.Get(),format.Get());
    Check(page.Rows().size()==1&&page.Rows()[0].title==L"Loot Route","metadata displayed without execution");
    const auto refresh=page.RefreshBounds();page.Down(refresh.left+10,refresh.top+10);Check(page.Up(refresh.left+10,refresh.top+10),"sole page action is Refresh");
    page.SetSnapshot(discovery.Refresh());Check(runtime.SessionCount()==0,"Refresh spawns zero sessions");
    Check(registry.Pages().size()==count&&!registry.Contains(PageId{"plugin.com.example.loot-route.main"}),"ui.page.register cannot change main UI");
    for(const auto state:{PluginState::InvalidManifest,PluginState::IncompatibleManifest,PluginState::IncompatibleApi,PluginState::DuplicateId,PluginState::UnsafePath}){
        auto invalid=discovery.Snapshot().records[0];invalid.state=state;Check(!runtime.Start(invalid),"invalid/incompatible/conflicting metadata cannot start a host");
    }
    Check(runtime.SessionCount()==0,"no metadata path starts a host");std::cout<<"Phase 1/2 no-execution boundary PASS\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
