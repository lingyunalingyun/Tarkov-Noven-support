#include "ui/ResourceDialog.h"
#include "ui/ValueFormat.h"
#include "ui/localization/LocalizationService.h"
#include "resources/ResourceFiles.h"
#include <commctrl.h>
#include <fstream>
#include <iostream>
using namespace noven;
void Check(bool value){if(!value)throw std::runtime_error("resource dialog assertion");}
namespace {
HWND owner{};bool inspected{},downloadTest{},managerTest{};unsigned ticks{};
resources::ResourceService* activeService{};
std::wstring Label(HWND dialog){wchar_t text[512]{};GetDlgItemTextW(dialog,103,text,512);return text;}
void CheckLayout(HWND dialog){
    const auto list=GetDlgItem(dialog,101);RECT client{};GetClientRect(list,&client);int total{};
    for(int i=0;i<7;++i)total+=ListView_GetColumnWidth(list,i);
    if(total<=client.right)Check(!(GetWindowLongW(list,GWL_STYLE)&WS_HSCROLL));
    RECT listRect{},button{};GetWindowRect(list,&listRect);GetWindowRect(GetDlgItem(dialog,IDCANCEL),&button);Check(listRect.bottom<button.top);
}
void CALLBACK Inspect(HWND,UINT,UINT_PTR timer,DWORD){
    HWND dialog{};EnumThreadWindows(GetCurrentThreadId(),[](HWND candidate,LPARAM result)->BOOL{if(GetWindow(candidate,GW_OWNER)!=owner)return TRUE;*reinterpret_cast<HWND*>(result)=candidate;return FALSE;},reinterpret_cast<LPARAM>(&dialog));
    if(!dialog||!IsWindowVisible(dialog)){if(++ticks>100)ExitProcess(2);return;}
    KillTimer(nullptr,timer);inspected=true;
    Check(!IsWindowEnabled(owner)&&!IsWindowEnabled(GetDlgItem(dialog,106)));
    Check((GetWindowLongW(GetDlgItem(dialog,IDCANCEL),GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW);
    CheckLayout(dialog);
    for(unsigned dpi:{96,144,192}){RECT rect{0,0,MulDiv(1200,dpi,96),MulDiv(650,dpi,96)};SendMessageW(dialog,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&rect));CheckLayout(dialog);}
    Check(Label(dialog).find(L"0 B")!=std::wstring::npos);
    if(downloadTest){
        SendMessageW(dialog,WM_COMMAND,104,0);const auto rows=activeService->Snapshot();
        Check(Label(dialog).find(ui::FormatBytes(rows.front().record.downloadSize))!=std::wstring::npos&&IsWindowEnabled(GetDlgItem(dialog,106)));
        SendMessageW(dialog,WM_COMMAND,105,0);Check(Label(dialog).find(L"0 B")!=std::wstring::npos&&!IsWindowEnabled(GetDlgItem(dialog,106)));
        SendMessageW(dialog,WM_COMMAND,104,0);PostMessageW(dialog,WM_COMMAND,106,0);
    }else if(managerTest){
        ListView_SetItemState(GetDlgItem(dialog,101),0,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(dialog,WM_TIMER,1,0);
        wchar_t text[128]{};GetDlgItemTextW(dialog,200,text,128);Check(std::wstring(text)==ui::Tr("resources.verify"));
        GetDlgItemTextW(dialog,201,text,128);Check(std::wstring(text)==ui::Tr("resources.delete"));
        SendMessageW(dialog,WM_COMMAND,200,0);PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
    }else{
        if(GetDlgItem(dialog,104)){SendMessageW(dialog,WM_COMMAND,104,0);Check(Label(dialog).find(ui::Tr("resources.size_unknown"))!=std::wstring::npos&&!IsWindowEnabled(GetDlgItem(dialog,106)));}
        PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
    }
}
struct Offline final:resources::ResourceTransport {
    std::string bytes;
    struct Stream final:resources::ResourceStream {std::string bytes,identity;std::uint64_t start{},position{};
        std::uint64_t Offset() const override{return start;}std::uint64_t TotalSize() const override{return bytes.size();}std::string Identity() const override{return identity;}
        std::size_t Read(std::span<char> buffer,std::stop_token stop) override{if(stop.stop_requested())return 0;const auto count=std::min(buffer.size(),bytes.size()-static_cast<std::size_t>(position));std::copy_n(bytes.data()+position,count,buffer.data());position+=count;return count;}
    };
    std::unique_ptr<resources::ResourceStream> Open(const resources::ResourceRecord& record,std::uint64_t offset,std::stop_token) override{auto stream=std::make_unique<Stream>();stream->bytes=bytes;stream->identity=record.sha256;stream->start=stream->position=offset;return stream;}
};
void Number(std::string& bytes,std::uint64_t value,unsigned count){for(unsigned i=0;i<count;++i)bytes+=static_cast<char>((value>>(8*i))&255);}
}
int wmain(int argc,wchar_t** argv) try {
    Check(argc==2);std::wstring error;Check(ui::UiLocalization().DiscoverLocales(argv[1],error)&&ui::UiLocalization().SetLocale("en-US"));
    Check(ui::FormatBytes(0)==L"0 B"&&ui::FormatBytes(2048)==L"2 KiB"&&ui::FormatBytes(842ULL*1024*1024)==L"842 MiB");
    Check(ui::FormatBytes(11162ULL*1024*1024)==L"10.9 GiB");
    resources::ResourceSnapshot a,b;a.record.resourceId="maps.factory";a.record.downloadSize=10;a.state=resources::ResourceState::NotInstalled;
    b.record.resourceId="maps.woods";b.record.downloadSize=20;b.state=resources::ResourceState::NotInstalled;
    const std::vector rows{a,b};ui::ResourceDialogSelection choice;choice.All(rows);Check(choice.Total(rows)==30);
    const auto decision=choice.Download(rows,true);Check(decision.download&&decision.ids==std::vector<std::string>{"maps.factory","maps.woods"});
    Check(!choice.Download(rows,false).download);choice.Toggle(a,false);Check(choice.Total(rows)==20&&!choice.UnknownSize(rows));choice.None();Check(choice.Total(rows)==0&&!choice.UnknownSize(rows)&&!choice.Download(rows,true).download);
    auto unknown=a;unknown.record.downloadSize=0;choice.Toggle(unknown,true);Check(choice.UnknownSize({unknown})&&!choice.Download({unknown},true).download);choice.None();
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
    service.Shutdown();Check(!service.CanDownload()&&!std::filesystem::exists(root));
    // 真实原生选择框驱动离线安装，再从管理框验证；没有生产端点或真实地图素材。
    // The real native selector drives offline installation and manager verification, without production URLs/assets.
    std::filesystem::create_directories(root);auto transport=std::make_shared<Offline>();auto record=a.record;record.stableMapId="factory";record.titleZh=record.titleEn="Synthetic";record.version="1.0.0";record.artifact="synthetic.nvr";record.installedSize=3;
    auto& bytes=transport->bytes;bytes="NVR1";Number(bytes,record.resourceId.size(),4);bytes+=record.resourceId;Number(bytes,1,4);const std::string path="maps/synthetic.png";Number(bytes,path.size(),4);Number(bytes,3,8);bytes+=path+"PNG";
    {std::ofstream file(root/record.artifact,std::ios::binary);file<<bytes;}record.downloadSize=bytes.size();record.sha256=resources::ResourceHash(root/record.artifact);
    {
        resources::ResourceService offline(common::AppPaths::Test(root/"program",root/"test"),{{record}},transport,[]{return 1024*1024ULL;});activeService=&offline;downloadTest=true;inspected=false;ticks=0;
        auto timer=SetTimer(nullptr,0,20,Inspect);const auto selected=ui::ShowResourceDialog(owner,offline,root,true);KillTimer(nullptr,timer);Check(inspected&&selected.download&&selected.ids==std::vector{record.resourceId});
        Check(offline.Act(selected.ids.front(),A::Download)&&offline.WaitIdle(std::chrono::seconds(10))&&offline.Snapshot().front().state==S::Installed);
        downloadTest=false;managerTest=true;inspected=false;ticks=0;timer=SetTimer(nullptr,0,20,Inspect);Check(!ui::ShowResourceDialog(owner,offline,root,false).download);KillTimer(nullptr,timer);
        Check(inspected&&offline.WaitIdle(std::chrono::seconds(10))&&offline.Snapshot().front().state==S::Installed);offline.Shutdown();Check(!offline.CanDownload());
    }
    DestroyWindow(owner);std::filesystem::remove_all(root);
    std::cout<<"selection/stable IDs/total/later/unconfigured/context actions/localized state PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what();return 1;}
