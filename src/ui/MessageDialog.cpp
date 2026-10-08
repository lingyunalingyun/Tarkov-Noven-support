#include "ui/MessageDialog.h"
#include "ui/Theme.h"

#include <dwmapi.h>
#include <commctrl.h>
#include <algorithm>
#include <string>

namespace noven::ui {
namespace {
constexpr int kBody = 100, kTitle = 101, kIcon = 102;
COLORREF Color(D2D1_COLOR_F color) {
    return RGB(static_cast<BYTE>(color.r*255), static_cast<BYTE>(color.g*255), static_cast<BYTE>(color.b*255));
}
// 自绘按钮仍向系统声明推按钮语义，保留 Tab/Enter；默认样式变化不能覆盖主题绘制。
// Owner-drawn buttons keep native Tab/Enter push-button semantics without losing themed drawing.
LRESULT CALLBACK ButtonProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR id,DWORD_PTR) {
    if(message==WM_GETDLGCODE)return DefSubclassProc(window,message,wparam,lparam)|
        (GetFocus()==window?DLGC_DEFPUSHBUTTON:DLGC_UNDEFPUSHBUTTON);
    if(message==BM_SETSTYLE){InvalidateRect(window,nullptr,TRUE);return 0;}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,ButtonProcedure,id);
    return DefSubclassProc(window,message,wparam,lparam);
}
struct Dialog final {
    std::wstring title, text, accept, cancel;
    MessageKind kind{};
    UiTheme theme;
    HBRUSH background{};
    HFONT font{}, heading{};
    UINT dpi{96};
    ~Dialog() { DeleteObject(background); DeleteObject(font); DeleteObject(heading); }
    int Px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
    void Fonts(HWND window) {
        DeleteObject(font); DeleteObject(heading);
        font=CreateFontW(-Px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        heading=CreateFontW(-Px(22),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(const int id:{kBody,IDOK,IDCANCEL})SendDlgItemMessageW(window,id,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        SendDlgItemMessageW(window,kTitle,WM_SETFONT,reinterpret_cast<WPARAM>(heading),TRUE);
    }
    void Layout(HWND window) const {
        RECT rect{};GetClientRect(window,&rect);
        const int pad=Px(24), footer=Px(76), button=Px(132), gap=Px(12), height=Px(40);
        MoveWindow(GetDlgItem(window,kIcon),pad,pad,Px(32),Px(32),TRUE);
        MoveWindow(GetDlgItem(window,kTitle),pad+Px(44),pad,std::max<LONG>(1,rect.right-2*pad-Px(44)),Px(40),TRUE);
        // 正文是可选择、可滚动的原生只读控件；独立底栏始终保留操作与键盘可访问性。
        // A native read-only body retains selection/scrolling; the separate footer keeps actions accessible.
        MoveWindow(GetDlgItem(window,kBody),pad,Px(76),std::max<LONG>(1,rect.right-2*pad),
            std::max<LONG>(1,rect.bottom-footer-Px(88)),TRUE);
        const int right=rect.right-pad, top=rect.bottom-footer+Px(18);
        MoveWindow(GetDlgItem(window,IDOK),right-button-(cancel.empty()?0:button+gap),top,button,height,TRUE);
        if(!cancel.empty())MoveWindow(GetDlgItem(window,IDCANCEL),right-button,top,button,height,TRUE);
        InvalidateRect(window,nullptr,TRUE);
    }
    void Button(const DRAWITEMSTRUCT& item) const {
        const bool down=(item.itemState&ODS_SELECTED)!=0;
        const auto fill=CreateSolidBrush(Color(down?theme.hover:theme.selected));
        const auto pen=CreatePen(PS_SOLID,Px(1),Color(theme.divider));
        const auto oldBrush=SelectObject(item.hDC,fill),oldPen=SelectObject(item.hDC,pen);
        RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,Px(16),Px(16));
        SelectObject(item.hDC,oldBrush);SelectObject(item.hDC,oldPen);DeleteObject(fill);DeleteObject(pen);
        SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,Color(theme.accent));
        const auto oldFont=SelectObject(item.hDC,font);
        const auto& label=item.CtlID==IDOK?accept:cancel;auto textRect=item.rcItem;
        DrawTextW(item.hDC,label.c_str(),static_cast<int>(label.size()),&textRect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(item.hDC,oldFont);
        if(item.itemState&ODS_FOCUS){auto focus=item.rcItem;InflateRect(&focus,-Px(5),-Px(5));DrawFocusRect(item.hDC,&focus);}
    }
};
INT_PTR CALLBACK Procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto* dialog=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG) {
        dialog=reinterpret_cast<Dialog*>(lparam);SetWindowLongPtrW(window,DWLP_USER,lparam);
        dialog->dpi=GetDpiForWindow(window);
        dialog->background=CreateSolidBrush(Color(dialog->theme.background));
        SetWindowTextW(window,dialog->title.c_str());
        const BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        const auto caption=Color(dialog->theme.sidebar),text=Color(dialog->theme.primaryText);
        DwmSetWindowAttribute(window,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
        DwmSetWindowAttribute(window,DWMWA_TEXT_COLOR,&text,sizeof(text));
        const auto instance=GetModuleHandleW(nullptr);
        const auto icon=CreateWindowExW(0,L"STATIC",nullptr,WS_CHILD|WS_VISIBLE|SS_ICON,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIcon)),instance,nullptr);
        SendMessageW(icon,STM_SETICON,reinterpret_cast<WPARAM>(LoadIconW(nullptr,
            dialog->kind==MessageKind::Error?IDI_ERROR:dialog->kind==MessageKind::Warning?IDI_WARNING:IDI_INFORMATION)),0);
        CreateWindowExW(0,L"STATIC",dialog->title.c_str(),WS_CHILD|WS_VISIBLE|SS_NOPREFIX,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTitle)),instance,nullptr);
        CreateWindowExW(0,L"EDIT",dialog->text.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
            0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBody)),instance,nullptr);
        CreateWindowExW(0,L"BUTTON",dialog->accept.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            0,0,0,0,window,reinterpret_cast<HMENU>(IDOK),instance,nullptr);
        if(!dialog->cancel.empty())CreateWindowExW(0,L"BUTTON",dialog->cancel.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
            0,0,0,0,window,reinterpret_cast<HMENU>(IDCANCEL),instance,nullptr);
        if(!GetDlgItem(window,kBody)||!GetDlgItem(window,IDOK)||(!dialog->cancel.empty()&&!GetDlgItem(window,IDCANCEL))) {
            EndDialog(window,IDCANCEL);return TRUE;
        }
        if(!SetWindowSubclass(GetDlgItem(window,IDOK),ButtonProcedure,1,0)||
            (!dialog->cancel.empty()&&!SetWindowSubclass(GetDlgItem(window,IDCANCEL),ButtonProcedure,1,0))) {
            EndDialog(window,IDCANCEL);return TRUE;
        }
        dialog->Fonts(window);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(GetWindow(window,GW_OWNER),MONITOR_DEFAULTTONEAREST),&monitor);
        const int width=std::min<LONG>(dialog->Px(720),monitor.rcWork.right-monitor.rcWork.left);
        const int height=std::min<LONG>(dialog->Px(560),monitor.rcWork.bottom-monitor.rcWork.top);
        RECT anchor=monitor.rcWork;const auto owner=GetWindow(window,GW_OWNER);if(owner)GetWindowRect(owner,&anchor);
        const int x=std::clamp((anchor.left+anchor.right-width)/2,monitor.rcWork.left,monitor.rcWork.right-width);
        const int y=std::clamp((anchor.top+anchor.bottom-height)/2,monitor.rcWork.top,monitor.rcWork.bottom-height);
        SetWindowPos(window,nullptr,x,y,width,height,SWP_NOZORDER);
        dialog->Layout(window);
        const int initial=dialog->cancel.empty()?IDOK:IDCANCEL;
        SendMessageW(window,DM_SETDEFID,initial,0);SetFocus(GetDlgItem(window,initial));return FALSE;
    }
    if(!dialog)return FALSE;
    switch(message) {
    case WM_COMMAND:
        if((LOWORD(wparam)==IDOK||LOWORD(wparam)==IDCANCEL)&&HIWORD(wparam)==BN_CLICKED){EndDialog(window,LOWORD(wparam));return TRUE;}break;
    case WM_CLOSE:EndDialog(window,IDCANCEL);return TRUE;
    case WM_SIZE:dialog->Layout(window);return TRUE;
    case WM_DPICHANGED: {
        dialog->dpi=HIWORD(wparam);dialog->Fonts(window);const auto* rect=reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER);return TRUE;
    }
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lparam);info->ptMinTrackSize={dialog->Px(460),dialog->Px(320)};return TRUE;
    }
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT: {
        const auto dc=reinterpret_cast<HDC>(wparam);
        SetBkColor(dc,Color(dialog->theme.background));
        SetTextColor(dc,Color(GetDlgCtrlID(reinterpret_cast<HWND>(lparam))==kTitle?dialog->theme.accent:dialog->theme.primaryText));
        return reinterpret_cast<INT_PTR>(dialog->background);
    }
    case WM_DRAWITEM:dialog->Button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));return TRUE;
    case WM_ERASEBKGND: {
        RECT rect{};GetClientRect(window,&rect);FillRect(reinterpret_cast<HDC>(wparam),&rect,dialog->background);return TRUE;
    }
    }
    return FALSE;
}
}
bool ShowMessageDialog(HWND owner,std::wstring_view title,std::wstring_view text,
    MessageKind kind,std::wstring_view accept,std::wstring_view cancel) {
    Dialog dialog;dialog.title=title;dialog.kind=kind;dialog.accept=accept;dialog.cancel=cancel;
    for(std::size_t i=0;i<text.size();++i) {
        if(text[i]==L'\n'&&(i==0||text[i-1]!=L'\r'))dialog.text+=L'\r';
        dialog.text+=text[i];
    }
    // 无资源模板避免语言相关的布局；系统模态循环继续处理消息并禁用父窗口。
    // A resource-free template avoids locale-specific geometry; the system modal loop disables the owner.
    struct Template { DLGTEMPLATE header; WORD menu{},windowClass{},title{}; } spec{};
    spec.header.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|DS_MODALFRAME;
    const auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&spec.header,owner,Procedure,reinterpret_cast<LPARAM>(&dialog));
    if(result==-1)OutputDebugStringW(L"[ui] Could not create themed message dialog; no confirmation granted.\n");
    return result==IDOK;
}
}
