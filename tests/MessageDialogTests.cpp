#include "ui/MessageDialog.h"
#include "ui/Theme.h"
#include <iostream>
#include <string>

namespace {
HWND owner{};
int mode{}, failures{}, ticks{};
bool inspected{};
HWND initialized{};
HWND initialFocus{};
unsigned earlyInspections{};
LRESULT CALLBACK Initialized(int code,WPARAM wparam,LPARAM lparam){
    if(code>=0){const auto* message=reinterpret_cast<const CWPRETSTRUCT*>(lparam);
        if(message->message==WM_INITDIALOG&&GetWindow(message->hwnd,GW_OWNER)==owner){initialized=message->hwnd;initialFocus=GetFocus();}}
    return CallNextHookEx(nullptr,code,wparam,lparam);
}
void Check(bool value,const char* label) { if(!value){std::cerr<<label<<'\n';++failures;} }
void CALLBACK Inspect(HWND,UINT,UINT_PTR timer,DWORD) {
    HWND dialog{};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND candidate,LPARAM result)->BOOL {
        if(GetWindow(candidate,GW_OWNER)!=owner)return TRUE;
        *reinterpret_cast<HWND*>(result)=candidate;return FALSE;
    },reinterpret_cast<LPARAM>(&dialog));
    // 初始化焦点在 WM_INITDIALOG 返回时取样；可见后的激活/用户消息可以合法改变焦点。
    // Sample initial focus at WM_INITDIALOG return, not after later activation/user messages.
    if(dialog&&IsWindowVisible(dialog)&&initialized!=dialog)++earlyInspections;
    if(!dialog||!IsWindowVisible(dialog)||initialized!=dialog) { if(++ticks>100)ExitProcess(2);return; }
    KillTimer(nullptr,timer);inspected=true;
    Check(!IsWindowEnabled(owner),"owner is disabled during modal message");
    const auto defaultId=SendMessageW(dialog,DM_GETDEFID,0,0);
    Check(HIWORD(defaultId)==DC_HASDEFID&&LOWORD(defaultId)==(mode==4?IDOK:IDCANCEL),"dialog manager default remains fail-closed regardless of foreground activation");
    if(initialFocus!=GetDlgItem(dialog,mode==4?IDOK:IDCANCEL))std::cerr<<"initial focus mode="<<mode<<" control="<<GetDlgCtrlID(initialFocus)<<'\n';
    Check(initialFocus==GetDlgItem(dialog,mode==4?IDOK:IDCANCEL),"initialized focus is cancel for consent, OK for information");
    Check(GetWindowTextLengthW(GetDlgItem(dialog,100))>10000,"long warning is retained without truncation");
    wchar_t content[64]{};GetDlgItemTextW(dialog,100,content,64);
    Check(std::wstring(content).find(L"\r\n")!=std::wstring::npos,"line feeds are normalized for native multiline text");
    Check(SendDlgItemMessageW(dialog,102,STM_GETICON,0,0)!=0,"message severity remains visible");
    Check((GetWindowLongW(GetDlgItem(dialog,IDOK),GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW,"default-button handling preserves themed drawing");
    const auto dc=GetDC(dialog);SendMessageW(dialog,WM_CTLCOLORSTATIC,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(GetDlgItem(dialog,100)));
    const noven::ui::UiTheme theme;
    Check(GetBkColor(dc)==RGB(static_cast<BYTE>(theme.background.r*255),static_cast<BYTE>(theme.background.g*255),static_cast<BYTE>(theme.background.b*255)),"body reuses the application dark theme");
    ReleaseDC(dialog,dc);
    RECT body{},button{},client{};GetWindowRect(GetDlgItem(dialog,100),&body);GetWindowRect(GetDlgItem(dialog,IDOK),&button);
    GetClientRect(dialog,&client);MapWindowPoints(dialog,nullptr,reinterpret_cast<POINT*>(&client),2);
    Check(body.bottom<button.top&&button.bottom<=client.bottom,"fixed footer is separate from long body");
    Check((GetWindowLongW(GetDlgItem(dialog,100),GWL_STYLE)&(ES_READONLY|WS_VSCROLL))==(ES_READONLY|WS_VSCROLL),"body is read-only and scrollable");
    SetWindowPos(dialog,nullptr,0,0,520,360,SWP_NOMOVE|SWP_NOZORDER);
    GetWindowRect(GetDlgItem(dialog,100),&body);GetWindowRect(GetDlgItem(dialog,IDOK),&button);
    Check(body.bottom<button.top,"resizing preserves footer separation");
    SendMessageW(dialog,WM_NEXTDLGCTL,reinterpret_cast<WPARAM>(GetDlgItem(dialog,mode==4?IDOK:IDCANCEL)),TRUE);
    Check(GetFocus()==GetDlgItem(dialog,mode==4?IDOK:IDCANCEL),"keyboard test owns the intended control focus before sending input");
    switch(mode) {
    case 0:PostMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(dialog,IDOK)));break;
    case 1:PostMessageW(dialog,WM_CLOSE,0,0);break;
    case 2:case 4:PostMessageW(GetFocus(),WM_KEYDOWN,VK_RETURN,0);break;
    case 3:PostMessageW(GetFocus(),WM_KEYDOWN,VK_ESCAPE,0);break;
    case 5:SetFocus(GetDlgItem(dialog,IDOK));PostMessageW(GetFocus(),WM_KEYDOWN,VK_RETURN,0);break;
    case 6:SetFocus(GetDlgItem(dialog,100));PostMessageW(GetFocus(),WM_KEYDOWN,VK_RETURN,0);break;
    }
}
}
int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    owner=CreateWindowExW(0,L"STATIC",L"Dialog test owner",WS_OVERLAPPEDWINDOW,0,0,900,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    const std::wstring text=L"Native code is not sandboxed.\n"+std::wstring(20000,L'x');
    const auto hook=SetWindowsHookExW(WH_CALLWNDPROCRET,Initialized,nullptr,GetCurrentThreadId());
    Check(hook!=nullptr,"thread-local initialization observer installed");if(!hook)return 1;
    for(mode=0;mode<7;++mode) {
        inspected=false;initialized=nullptr;initialFocus=nullptr;ticks=0;const auto timer=SetTimer(nullptr,0,20,Inspect);
        const bool accepted=noven::ui::ShowMessageDialog(owner,L"Noven dialog test",text,
            mode==4?noven::ui::MessageKind::Information:mode==1?noven::ui::MessageKind::Error:noven::ui::MessageKind::Warning,
            L"Enable",mode==4?L"":L"Cancel");
        KillTimer(nullptr,timer);
        if(accepted!=(mode==0||mode==4||mode==5))std::cerr<<"mode="<<mode<<" accepted="<<accepted<<'\n';
        if(!inspected)std::cerr<<"uninspected mode="<<mode<<" error="<<GetLastError()<<'\n';
        Check(inspected,"dialog was created");Check(accepted==(mode==0||mode==4||mode==5),"close, Escape and default Enter never authorize consent");
        Check(IsWindowEnabled(owner),"owner restored after modal dialog");
    }
    UnhookWindowsHookEx(hook);DestroyWindow(owner);std::cout<<"pre-initialization timer observations="<<earlyInspections<<'\n';return failures?1:0;
}
