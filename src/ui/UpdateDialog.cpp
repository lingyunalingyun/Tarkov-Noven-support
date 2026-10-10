#include "ui/UpdateDialog.h"
#include "ui/Theme.h"
#include "ui/ValueFormat.h"
#include "ui/MessageDialog.h"
#include "ui/localization/LocalizationService.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <array>
namespace noven::ui {
namespace {
std::wstring Wide(std::string_view s){if(s.empty())return {};const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)return L"?";
    std::wstring text(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),text.data(),n);return text;}
COLORREF Color(D2D1_COLOR_F c){return RGB(static_cast<BYTE>(c.r*255),static_cast<BYTE>(c.g*255),static_cast<BYTE>(c.b*255));}
const char* State(updates::UpdateState s){using S=updates::UpdateState;switch(s){case S::Unconfigured:return "updates.unconfigured";case S::Checking:return "updates.checking";case S::Current:return "updates.current";
    case S::Available:return "updates.available";case S::Downloading:return "updates.downloading";case S::Paused:return "updates.paused";case S::Staged:return "updates.staged";case S::Error:return "updates.error";default:return "updates.idle";}}
struct Dialog {
    updates::UpdateService& service;HWND window{},body{};std::array<HWND,8> buttons{};UiTheme theme;HFONT font{};HBRUSH background{};bool restart{};std::wstring displayed;
    int Scale(int n) const {return MulDiv(n,static_cast<int>(GetDpiForWindow(window)),96);}
    void Font(unsigned dpi){if(font)DeleteObject(font);font=CreateFontW(-MulDiv(16,static_cast<int>(dpi),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        if(body)SendMessageW(body,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);for(auto button:buttons)if(button)SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
    void Layout(){RECT r{};GetClientRect(window,&r);const int pad=Scale(18),gap=Scale(8),h=Scale(38),w=(r.right-2*pad-2*gap)/3;
        MoveWindow(body,pad,pad,r.right-2*pad,std::max(Scale(80),static_cast<int>(r.bottom)-2*pad-3*(h+gap)-gap),TRUE);
        for(int i=0;i<8;++i)MoveWindow(buttons[i],pad+(i%3)*(w+gap),r.bottom-pad-3*(h+gap)+(i/3)*(h+gap),w,h,TRUE);}
    void Refresh(){const auto v=service.Snapshot();std::wstring text=Tr("updates.version")+L": "+Wide(v.current)+L"\r\n"+Tr(State(v.state));
        if(!v.target.empty())text+=L"\r\n"+Tr("updates.target")+L": "+Wide(v.target)+L"\r\n"+Tr("updates.download_size")+L": "+FormatBytes(v.downloadBytes)+L"\r\n"+Tr("updates.full_size")+L": "+FormatBytes(v.fullBytes)+L"\r\n"+Tr("updates.reuse")+L": "+FormatBytes(v.reusedBytes);
        if(v.state==updates::UpdateState::Downloading)text+=L"\r\n"+FormatBytes(v.received)+L" / "+FormatBytes(v.downloadBytes)+L" ("+std::to_wstring(v.downloadBytes?std::min<std::uint64_t>(100,v.received*100/v.downloadBytes):100)+L"%)"+(v.constructing?L"\r\n"+Tr("updates.verifying"):L"");
        if(!v.previous.empty())text+=L"\r\n"+Tr("updates.rollback")+L": "+Wide(v.previous);
        if(!v.notes.empty())text+=L"\r\n\r\n"+Wide(v.notes);if(!v.error.empty())text+=L"\r\n"+Wide(v.error);if(text!=displayed){displayed=text;SetWindowTextW(body,text.c_str());}
        using S=updates::UpdateState;const bool idle=v.state!=S::Unconfigured&&v.state!=S::Checking&&v.state!=S::Downloading;
        const bool enabled[]{idle,v.state==S::Available||(v.state==S::Error&&!v.target.empty()),v.state==S::Downloading,v.state==S::Paused,v.state==S::Downloading||v.state==S::Paused,v.state==S::Staged,!v.previous.empty()&&v.state!=S::Downloading,true};
        for(std::size_t i=0;i<buttons.size();++i)EnableWindow(buttons[i],enabled[i]);}
    static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM w,LPARAM l){auto* d=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){d=static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);d->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));}
        if(!d)return DefWindowProcW(window,message,w,l);
        if(message==WM_CREATE){BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));d->background=CreateSolidBrush(Color(d->theme.background));
            d->Font(GetDpiForWindow(window));
            d->body=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,0,0,0,0,window,nullptr,nullptr,nullptr);SendMessageW(d->body,WM_SETFONT,reinterpret_cast<WPARAM>(d->font),TRUE);
            SetWindowTheme(d->body,L"DarkMode_Explorer",nullptr);
            const char* labels[]{"updates.check","updates.download","resources.pause","resources.resume","resources.cancel","updates.restart","updates.rollback","dialog.ok"};
            for(int i=0;i<8;++i){d->buttons[i]=CreateWindowExW(0,L"BUTTON",Tr(labels[i]).c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(100+i)),nullptr,nullptr);SendMessageW(d->buttons[i],WM_SETFONT,reinterpret_cast<WPARAM>(d->font),TRUE);}
            d->Layout();d->Refresh();SetTimer(window,1,200,nullptr);return 0;}
        if(message==WM_TIMER){d->Refresh();return 0;}if(message==WM_SIZE){d->Layout();return 0;}
        if(message==WM_ERASEBKGND){RECT r{};GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(w),&r,d->background);return 1;}
        if(message==WM_GETMINMAXINFO){auto* m=reinterpret_cast<MINMAXINFO*>(l);m->ptMinTrackSize={d->Scale(520),d->Scale(430)};return 0;}
        if(message==WM_DPICHANGED){d->Font(HIWORD(w));const auto* r=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);return 0;}
        if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT){auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,Color(d->theme.primaryText));SetBkColor(dc,Color(d->theme.background));return reinterpret_cast<LRESULT>(d->background);}
        if(message==WM_DRAWITEM){const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);const auto brush=CreateSolidBrush(Color(d->theme.surface));FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,Color((item->itemState&ODS_DISABLED)?d->theme.secondaryText:d->theme.accent));
            wchar_t text[128]{};GetWindowTextW(item->hwndItem,text,128);auto r=item->rcItem;DrawTextW(item->hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&r);return TRUE;}
        if(message==WM_COMMAND){const auto id=LOWORD(w);using A=updates::UpdateAction;
            if(id>=100&&id<=104){const A actions[]{A::Check,A::Download,A::Pause,A::Resume,A::Cancel};d->service.Act(actions[id-100]);d->Refresh();return 0;}
            if(id==105||id==106){if(ShowMessageDialog(window,Tr("updates.manage"),Tr("updates.restart_confirm"),MessageKind::Warning,Tr("updates.restart"),Tr("resources.cancel"))&&d->service.LaunchApply(id==106)){d->restart=true;DestroyWindow(window);}return 0;}
            if(id==107){DestroyWindow(window);return 0;}}
        if(message==WM_CLOSE){DestroyWindow(window);return 0;}if(message==WM_DESTROY){KillTimer(window,1);return 0;}
        return DefWindowProcW(window,message,w,l);
    }
};
}
bool ShowUpdateDialog(HWND owner,updates::UpdateService& service){WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=Dialog::Procedure;cls.lpszClassName=L"NovenUpdateDialog";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
    Dialog dialog{service};const auto dpi=GetDpiForWindow(owner);HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,Tr("updates.manage").c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(700,dpi?dpi:96,96),MulDiv(550,dpi?dpi:96,96),owner,nullptr,cls.hInstance,&dialog);
    if(!window)return false;EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);MSG message{};bool quit{};
    while(IsWindow(window)){const auto got=GetMessageW(&message,nullptr,0,0);if(got<=0){quit=got==0;break;}if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    if(IsWindow(window))DestroyWindow(window);EnableWindow(owner,TRUE);SetActiveWindow(owner);if(dialog.font)DeleteObject(dialog.font);if(dialog.background)DeleteObject(dialog.background);if(quit)PostQuitMessage(static_cast<int>(message.wParam));return dialog.restart;
}
}
