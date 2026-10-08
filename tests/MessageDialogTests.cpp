#include "ui/MessageDialog.h"
#include "ui/Theme.h"
#include <iostream>
#include <string>

namespace {
HWND owner{};
int mode{}, failures{}, ticks{};
bool inspected{};
void Check(bool value,const char* label) { if(!value){std::cerr<<label<<'\n';++failures;} }
void CALLBACK Inspect(HWND,UINT,UINT_PTR timer,DWORD) {
    const auto dialog=FindWindowW(L"#32770",L"Noven dialog test");
    if(!dialog) { if(++ticks>100)ExitProcess(2);return; }
    KillTimer(nullptr,timer);inspected=true;
    Check(!IsWindowEnabled(owner),"owner is disabled during modal message");
    Check(GetFocus()==GetDlgItem(dialog,mode==4?IDOK:IDCANCEL),"default focus is cancel for consent, OK for information");
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
    for(mode=0;mode<7;++mode) {
        inspected=false;ticks=0;const auto timer=SetTimer(nullptr,0,20,Inspect);
        const bool accepted=noven::ui::ShowMessageDialog(owner,L"Noven dialog test",text,
            mode==4?noven::ui::MessageKind::Information:mode==1?noven::ui::MessageKind::Error:noven::ui::MessageKind::Warning,
            L"Enable",mode==4?L"":L"Cancel");
        KillTimer(nullptr,timer);
        if(accepted!=(mode==0||mode==4||mode==5))std::cerr<<"mode="<<mode<<" accepted="<<accepted<<'\n';
        Check(inspected,"dialog was created");Check(accepted==(mode==0||mode==4||mode==5),"close, Escape and default Enter never authorize consent");
        Check(IsWindowEnabled(owner),"owner restored after modal dialog");
    }
    DestroyWindow(owner);return failures?1:0;
}
