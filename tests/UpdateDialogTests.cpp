#include "ui/UpdateDialog.h"
#include "ui/localization/LocalizationService.h"
#include <iostream>
#include "UpdateTestSigning.h"
#include "updates/UpdateLocalSource.h"
#include "resources/ResourceFiles.h"
#include <commctrl.h>
#include <source_location>
using namespace noven;
namespace {
HWND owner{};bool inspected{};unsigned ticks{};
updates::UpdateSnapshot expected;
class Gate final:public updates::UpdateSource {
    std::shared_ptr<updates::UpdateSource> local_;
public:
    std::mutex mutex;std::condition_variable_any condition;bool released{};
    explicit Gate(const std::filesystem::path& path):local_(updates::LocalUpdateSource(path)){}
    std::string FetchManifest(std::stop_token stop)override{return local_->FetchManifest(stop);}
    std::unique_ptr<resources::ResourceStream> Open(std::string_view pack,std::uint64_t offset,std::uint64_t count,std::string_view identity,std::stop_token stop)override{
        std::unique_lock lock(mutex);if(!condition.wait(lock,stop,[&]{return released;}))throw std::runtime_error("paused test transfer");
        return local_->Open(pack,offset,count,identity,stop);
    }
    void Release(){std::lock_guard lock(mutex);released=true;condition.notify_all();}
};
void Check(bool value,std::source_location location=std::source_location::current()){if(!value)throw std::runtime_error("native update dialog assertion at line "+std::to_string(location.line()));}
void CALLBACK Inspect(HWND,UINT,UINT_PTR timer,DWORD)try{
    HWND dialog{};EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM result)->BOOL{
        if(GetWindow(window,GW_OWNER)!=owner)return TRUE;*reinterpret_cast<HWND*>(result)=window;return FALSE;},reinterpret_cast<LPARAM>(&dialog));
    if(!dialog||!IsWindowVisible(dialog)){if(++ticks>100)ExitProcess(2);return;}
    KillTimer(nullptr,timer);Check(!IsWindowEnabled(owner));
    Check(IsWindowEnabled(GetDlgItem(dialog,107)));
    const auto actions=ui::UpdateDialogActions(expected);
    for(int i=0;i<8;++i)Check(static_cast<bool>(IsWindowVisible(GetDlgItem(dialog,100+i)))==actions[i]);
    const bool transfer=expected.state==updates::UpdateState::Downloading||expected.state==updates::UpdateState::Paused||expected.state==updates::UpdateState::Staged;
    const auto progress=GetDlgItem(dialog,108);Check(static_cast<bool>(IsWindowVisible(progress))==transfer);
    Check(SendMessageW(progress,PBM_GETPOS,0,0)==(expected.state==updates::UpdateState::Staged?100:0));
    const auto body=FindWindowExW(dialog,nullptr,L"EDIT",nullptr);wchar_t text[2048]{};GetWindowTextW(body,text,2048);
    Check(std::wstring(text).find(L"0.1.0")!=std::wstring::npos);
    if(expected.state==updates::UpdateState::Unconfigured){Check(std::wstring(text).find(ui::Tr("updates.unconfigured"))!=std::wstring::npos);for(int i=100;i<107;++i)Check(!IsWindowEnabled(GetDlgItem(dialog,i)));}
    for(unsigned dpi:{96,144,192}){
        RECT resize{0,0,MulDiv(700,dpi,96),MulDiv(550,dpi,96)};SendMessageW(dialog,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&resize));
        RECT client{},bodyRect{};GetClientRect(dialog,&client);GetWindowRect(body,&bodyRect);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&bodyRect),2);
        RECT barRect{};if(transfer){GetWindowRect(progress,&barRect);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&barRect),2);Check(barRect.top>bodyRect.bottom&&barRect.right<=client.right);}
        for(int i=100;i<108;++i){const auto button=GetDlgItem(dialog,i);if(!IsWindowVisible(button))continue;RECT rect{};GetWindowRect(button,&rect);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&rect),2);
            Check(rect.left>=0&&rect.right<=client.right&&rect.top>bodyRect.bottom&&rect.bottom<=client.bottom);
            if(transfer)Check(rect.top>barRect.bottom);
            Check((GetWindowLongW(button,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW);}
        LOGFONTW font{};Check(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(body,WM_GETFONT,0,0)),sizeof(font),&font)&&font.lfHeight==-MulDiv(16,dpi,96));
    }
    inspected=true;PostMessageW(dialog,WM_COMMAND,107,0);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';ExitProcess(3);}
}
int wmain(int argc,wchar_t** argv)try{
    using S=updates::UpdateState;updates::UpdateSnapshot view;view.state=S::Available;
    Check(ui::UpdateDialogActions(view)==std::array<bool,8>{true,true,false,false,false,false,false,true});
    view.state=S::Downloading;Check(ui::UpdateDialogActions(view)==std::array<bool,8>{false,false,true,false,true,false,false,true});
    view.constructing=true;Check(ui::UpdateDialogActions(view)==std::array<bool,8>{false,false,false,false,false,false,false,true});
    view.constructing=false;view.state=S::Paused;Check(ui::UpdateDialogActions(view)==std::array<bool,8>{false,false,false,true,true,false,false,true});
    view.state=S::Staged;view.previous="0.0.9";Check(ui::UpdateDialogActions(view)==std::array<bool,8>{false,false,false,false,false,true,true,true});
    Check(argc==2);std::wstring error;Check(ui::UiLocalization().DiscoverLocales(argv[1],error));
    const auto root=std::filesystem::temp_directory_path()/("noven-update-dialog-"+std::to_string(GetCurrentProcessId()));Check(!std::filesystem::exists(root));
    updates::UpdateService service(common::AppPaths::Test(root/"program",root/"test"),{},"0.1.0",{});
    owner=CreateWindowExW(0,L"STATIC",L"Update test owner",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    const auto inspect=[&](updates::UpdateService& current){expected=current.Snapshot();for(const auto locale:{"en-US","zh-CN"}){Check(ui::UiLocalization().SetLocale(locale));inspected=false;ticks=0;const auto timer=SetTimer(nullptr,0,20,Inspect);
        Check(!ui::ShowUpdateDialog(owner,current));KillTimer(nullptr,timer);Check(inspected&&IsWindowEnabled(owner));}};
    inspect(service);service.Shutdown();Check(!std::filesystem::exists(root));
    const auto program=root/"program",old=program/"versions"/"0.1.0",fixture=root/"fixture";
    Check(resources::WriteResourceText(old/"NovenTarkovSupport.exe","old"));Check(resources::WriteResourceText(old/"NovenPluginHost.exe","host"));
    tests::UpdateSigner signer;updates::VersionStore(program,"0.1.0",{signer.publicKey}).SeedInitial();
    Check(resources::WriteResourceText(fixture/"core.pack","new apphost"));
    updates::ReleaseManifest manifest{"0.1.1","2026-10-10T00:00:00Z","",{}};
    manifest.files.push_back({"NovenTarkovSupport.exe",updates::HashFileRange(fixture/"core.pack",0,7),7,{{0,7,0,updates::HashFileRange(fixture/"core.pack",0,7),"core.pack"}}});
    manifest.files.push_back({"NovenPluginHost.exe",updates::HashFileRange(fixture/"core.pack",7,4),4,{{0,4,7,updates::HashFileRange(fixture/"core.pack",7,4),"core.pack"}}});
    Check(resources::WriteResourceText(fixture/"release.json",signer.Sign(updates::EncodeReleaseManifest(manifest))));
    auto source=std::make_shared<Gate>(fixture);updates::UpdateService active(common::AppPaths::Test(old,root/"test"),program,"0.1.0",{signer.publicKey},source);
    Check(active.Act(updates::UpdateAction::Check)&&active.WaitIdle(std::chrono::seconds(10)));inspect(active);
    Check(active.Act(updates::UpdateAction::Download));inspect(active);
    Check(active.Act(updates::UpdateAction::Pause)&&active.WaitIdle(std::chrono::seconds(10)));inspect(active);
    source->Release();Check(active.Act(updates::UpdateAction::Resume)&&active.WaitIdle(std::chrono::seconds(10)));Check(active.Snapshot().state==S::Staged);inspect(active);
    active.Shutdown();DestroyWindow(owner);std::filesystem::remove_all(root);
    std::cout<<"native update dialog state actions/progress/localization/DPI/geometry PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
