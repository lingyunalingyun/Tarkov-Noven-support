#include "ui/UpdateDialog.h"
#include "ui/localization/LocalizationService.h"
#include <iostream>
using namespace noven;
namespace {
HWND owner{};bool inspected{};unsigned ticks{};
void Check(bool value){if(!value)throw std::runtime_error("native update dialog assertion");}
void CALLBACK Inspect(HWND,UINT,UINT_PTR timer,DWORD)try{
    HWND dialog{};EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM result)->BOOL{
        if(GetWindow(window,GW_OWNER)!=owner)return TRUE;*reinterpret_cast<HWND*>(result)=window;return FALSE;},reinterpret_cast<LPARAM>(&dialog));
    if(!dialog||!IsWindowVisible(dialog)){if(++ticks>100)ExitProcess(2);return;}
    KillTimer(nullptr,timer);Check(!IsWindowEnabled(owner));
    for(int i=100;i<107;++i)Check(!IsWindowEnabled(GetDlgItem(dialog,i)));
    Check(IsWindowEnabled(GetDlgItem(dialog,107)));
    const auto body=FindWindowExW(dialog,nullptr,L"EDIT",nullptr);wchar_t text[2048]{};GetWindowTextW(body,text,2048);
    Check(std::wstring(text).find(ui::Tr("updates.unconfigured"))!=std::wstring::npos&&std::wstring(text).find(L"0.1.0")!=std::wstring::npos);
    for(unsigned dpi:{96,144,192}){
        RECT resize{0,0,MulDiv(700,dpi,96),MulDiv(550,dpi,96)};SendMessageW(dialog,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&resize));
        RECT client{},bodyRect{};GetClientRect(dialog,&client);GetWindowRect(body,&bodyRect);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&bodyRect),2);
        for(int i=100;i<108;++i){const auto button=GetDlgItem(dialog,i);RECT rect{};GetWindowRect(button,&rect);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&rect),2);
            Check(rect.left>=0&&rect.right<=client.right&&rect.top>bodyRect.bottom&&rect.bottom<=client.bottom);
            Check((GetWindowLongW(button,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW);}
        LOGFONTW font{};Check(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(body,WM_GETFONT,0,0)),sizeof(font),&font)&&font.lfHeight==-MulDiv(16,dpi,96));
    }
    inspected=true;PostMessageW(dialog,WM_COMMAND,107,0);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';ExitProcess(3);}
}
int wmain(int argc,wchar_t** argv)try{
    Check(argc==2);std::wstring error;Check(ui::UiLocalization().DiscoverLocales(argv[1],error));
    const auto root=std::filesystem::temp_directory_path()/("noven-update-dialog-"+std::to_string(GetCurrentProcessId()));Check(!std::filesystem::exists(root));
    updates::UpdateService service(common::AppPaths::Test(root/"program",root/"test"),{},"0.1.0",{});
    owner=CreateWindowExW(0,L"STATIC",L"Update test owner",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    for(const auto locale:{"en-US","zh-CN"}){Check(ui::UiLocalization().SetLocale(locale));inspected=false;ticks=0;const auto timer=SetTimer(nullptr,0,20,Inspect);
        Check(!ui::ShowUpdateDialog(owner,service));KillTimer(nullptr,timer);Check(inspected&&IsWindowEnabled(owner));}
    DestroyWindow(owner);service.Shutdown();Check(!std::filesystem::exists(root));std::cout<<"native update dialog capability/localization/DPI/geometry PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
