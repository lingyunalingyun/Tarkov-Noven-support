#include "ui/ResourceDialog.h"
#include "ui/localization/LocalizationService.h"
#include <iostream>
using namespace noven;
void Check(bool value){if(!value)throw std::runtime_error("resource dialog assertion");}
namespace {
HWND owner{};bool inspected{};unsigned ticks{};
void CALLBACK Inspect(HWND,UINT,UINT_PTR timer,DWORD){
    HWND dialog{};EnumThreadWindows(GetCurrentThreadId(),[](HWND candidate,LPARAM result)->BOOL{if(GetWindow(candidate,GW_OWNER)!=owner)return TRUE;*reinterpret_cast<HWND*>(result)=candidate;return FALSE;},reinterpret_cast<LPARAM>(&dialog));
    if(!dialog||!IsWindowVisible(dialog)){if(++ticks>100)ExitProcess(2);return;}
    KillTimer(nullptr,timer);inspected=true;
    Check(!IsWindowEnabled(owner)&&!IsWindowEnabled(GetDlgItem(dialog,106)));
    Check((GetWindowLongW(GetDlgItem(dialog,IDCANCEL),GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW);
    RECT list{},button{};GetWindowRect(GetDlgItem(dialog,101),&list);GetWindowRect(GetDlgItem(dialog,IDCANCEL),&button);Check(list.bottom<button.top);
    PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
}
}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==2);std::wstring error;Check(ui::UiLocalization().DiscoverLocales(argv[1],error)&&ui::UiLocalization().SetLocale("en-US"));
    resources::ResourceSnapshot a,b;a.record.resourceId="maps.factory";a.record.downloadSize=10;a.state=resources::ResourceState::NotInstalled;
    b.record.resourceId="maps.woods";b.record.downloadSize=20;b.state=resources::ResourceState::NotInstalled;
    const std::vector rows{a,b};ui::ResourceDialogSelection choice;choice.All(rows);Check(choice.Total(rows)==30);
    const auto decision=choice.Download(rows,resources::RemoteAvailability::OfflineFixture);Check(decision.download&&decision.ids==std::vector<std::string>{"maps.factory","maps.woods"});
    Check(!choice.Download(rows,resources::RemoteAvailability::ProductionEndpointUnconfigured).download);choice.Toggle(a,false);Check(choice.Total(rows)==20);choice.None();Check(choice.Total(rows)==0&&!choice.Download(rows,resources::RemoteAvailability::OfflineFixture).download);
    using S=resources::ResourceState;using A=resources::ResourceAction;
    for(const auto state:{S::Unavailable,S::NotInstalled,S::Queued,S::Downloading,S::Paused,S::Verifying,S::Installing,S::Installed,S::UpdateAvailable,S::Deleting,S::Error})Check(!ui::Tr(ui::ResourceStateKey(state)).empty());
    for(const auto action:{A::Download,A::Pause,A::Resume,A::Cancel,A::Verify,A::Repair,A::Delete})Check(!ui::Tr(ui::ResourceActionKey(action)).empty());
    a.state=S::Downloading;Check(resources::ResourceActions(a,resources::RemoteAvailability::OfflineFixture)==std::vector{A::Pause,A::Cancel});
    a.state=S::Installed;Check(resources::ResourceActions(a,resources::RemoteAvailability::ProductionEndpointUnconfigured)==std::vector{A::Verify,A::Delete});
    a.state=S::Unavailable;Check(resources::ResourceActions(a,resources::RemoteAvailability::ProductionEndpointUnconfigured).empty());
    const auto root=std::filesystem::temp_directory_path()/("noven-resource-dialog-"+std::to_string(GetCurrentProcessId()));Check(!std::filesystem::exists(root));
    resources::ResourceService service(common::AppPaths::Test(root/"program",root/"user"),{}, {},{}, {},{{"factory","工厂","Factory"}});
    owner=CreateWindowExW(0,L"STATIC",L"Resource test owner",WS_OVERLAPPEDWINDOW,0,0,900,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    for(bool onboarding:{true,false}){inspected=false;ticks=0;const auto timer=SetTimer(nullptr,0,20,Inspect);Check(!ui::ShowResourceDialog(owner,service,root,onboarding).download);KillTimer(nullptr,timer);Check(inspected&&IsWindowEnabled(owner));}
    DestroyWindow(owner);service.Shutdown();Check(!std::filesystem::exists(root));
    std::cout<<"selection/stable IDs/total/later/unconfigured/context actions/localized state PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}
