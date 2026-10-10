#include "ui/ResourceDialog.h"
#include "ui/MessageDialog.h"
#include "ui/Theme.h"
#include "ui/ValueFormat.h"
#include "ui/localization/LocalizationService.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
namespace noven::ui {
ResourceDialogDecision ResourceDialogSelection::Download(const std::vector<resources::ResourceSnapshot>& rows,bool canDownload) const {
    ResourceDialogDecision result;if(!canDownload)return result;
    for(const auto& row:rows)if(Selected(row.record.resourceId)&&row.record.downloadSize&&!row.installed&&
        (row.state==resources::ResourceState::NotInstalled||row.state==resources::ResourceState::Unavailable||row.state==resources::ResourceState::Error))result.ids.push_back(row.record.resourceId);
    result.download=!result.ids.empty();return result;
}
std::string_view ResourceStateKey(resources::ResourceState state){using S=resources::ResourceState;switch(state){
    case S::Unavailable:return "resources.unavailable";case S::NotInstalled:return "resources.not_installed";case S::Queued:return "resources.queued";
    case S::Downloading:return "resources.downloading";case S::Paused:return "resources.paused";case S::Verifying:return "resources.verifying";
    case S::Installing:return "resources.installing";case S::Installed:return "resources.installed";case S::UpdateAvailable:return "resources.update_available";
    case S::Deleting:return "resources.deleting";case S::Error:return "resources.failed";}return "resources.failed";}
std::string_view ResourceActionKey(resources::ResourceAction action){using A=resources::ResourceAction;switch(action){
    case A::Download:return "resources.download";case A::Pause:return "resources.pause";case A::Resume:return "resources.resume";
    case A::Cancel:return "resources.cancel";case A::Verify:return "resources.verify";case A::Repair:return "resources.repair";case A::Delete:return "resources.delete";}return "resources.failed";}
namespace {
constexpr int List=101,Note=102,Total=103,All=104,None=105,Download=106,Check=107,Clear=108,Action=200;
LRESULT CALLBACK ButtonProcedure(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR){
    if(message==WM_GETDLGCODE)return DefSubclassProc(window,message,w,l)|(GetFocus()==window?DLGC_DEFPUSHBUTTON:DLGC_UNDEFPUSHBUTTON);
    if(message==BM_SETSTYLE){InvalidateRect(window,nullptr,TRUE);return 0;}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,ButtonProcedure,id);return DefSubclassProc(window,message,w,l);
}
COLORREF Color(D2D1_COLOR_F c){return RGB(static_cast<BYTE>(c.r*255),static_cast<BYTE>(c.g*255),static_cast<BYTE>(c.b*255));}
std::wstring Wide(std::string_view text){const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);std::wstring result(static_cast<std::size_t>(count),L'\0');if(count)MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),count);return result;}
std::wstring Size(std::uint64_t bytes){return bytes?FormatBytes(bytes):Tr("resources.size_unknown");}
struct Dialog {
    resources::ResourceService& service;std::filesystem::path root;bool onboarding{},updating{};
    ResourceDialogSelection selection;ResourceDialogDecision decision;std::vector<resources::ResourceSnapshot> rows;
    std::vector<resources::ResourceAction> actions;int selected{-1};UiTheme theme;HBRUSH background{};HFONT font{};UINT dpi{96};
    ~Dialog(){DeleteObject(background);DeleteObject(font);}
    int Px(int value) const{return MulDiv(value,static_cast<int>(dpi),96);}
    void Font(HWND window){
        const auto previous=font;font=CreateFontW(-Px(16),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        EnumChildWindows(window,[](HWND child,LPARAM value)->BOOL{SendMessageW(child,WM_SETFONT,static_cast<WPARAM>(value),TRUE);return TRUE;},reinterpret_cast<LPARAM>(font));DeleteObject(previous);
    }
    void Refresh(HWND window){
        rows=service.Snapshot();updating=true;const auto list=GetDlgItem(window,List);
        if(ListView_GetItemCount(list)!=static_cast<int>(rows.size())){ListView_DeleteAllItems(list);for(std::size_t i=0;i<rows.size();++i){LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(i);item.pszText=const_cast<wchar_t*>(L"");ListView_InsertItem(list,&item);}}
        for(std::size_t i=0;i<rows.size();++i){const auto& row=rows[i];std::wstring columns[]{Wide(UiLocalization().ActiveLocale()=="zh-CN"?row.record.titleZh:row.record.titleEn),Tr(ResourceStateKey(row.state)),Size(row.record.downloadSize),row.record.downloadSize?std::to_wstring(row.received*100/row.record.downloadSize)+L"%":L"-",Size(row.record.installedSize),Wide(row.record.version), row.error.empty()?L"":Tr(row.error)};
            for(int col=0;col<7;++col)ListView_SetItemText(list,static_cast<int>(i),col,columns[col].data());
            if(onboarding)ListView_SetCheckState(list,static_cast<int>(i),selection.Selected(row.record.resourceId));}
        updating=false;
        std::wstring total=Tr("resources.selected_total")+L" "+(selection.UnknownSize(rows)?Tr("resources.size_unknown"):FormatBytes(selection.Total(rows)));
        auto existing=root;while(!std::filesystem::exists(existing)&&existing!=existing.root_path())existing=existing.parent_path();ULARGE_INTEGER free{};
        if(GetDiskFreeSpaceExW(existing.c_str(),&free,nullptr,nullptr))total+=L" · "+Tr("resources.disk_free")+L" "+FormatBytes(free.QuadPart);
        SetDlgItemTextW(window,Total,total.c_str());SetDlgItemTextW(window,Note,Tr(service.Remote()==resources::RemoteAvailability::ProductionEndpointUnconfigured?"resources.unconfigured":"resources.review_only").c_str());
        ResourceDialogSelection all;all.All(rows);
        EnableWindow(GetDlgItem(window,Download),(onboarding?selection:all).Download(rows,service.CanDownload()).download);
        actions.clear();if(!onboarding&&selected>=0&&static_cast<std::size_t>(selected)<rows.size())actions=resources::ResourceActions(rows[static_cast<std::size_t>(selected)],service.Remote());
        for(int i=0;i<4;++i){const auto button=GetDlgItem(window,Action+i);ShowWindow(button,static_cast<std::size_t>(i)<actions.size()?SW_SHOW:SW_HIDE);if(static_cast<std::size_t>(i)<actions.size())SetWindowTextW(button,Tr(ResourceActionKey(actions[static_cast<std::size_t>(i)])).c_str());}
    }
    void Layout(HWND window){RECT rect{};GetClientRect(window,&rect);const int w=rect.right,h=rect.bottom,p=Px(20),gap=Px(8),button=Px(132),bh=Px(38);
        MoveWindow(GetDlgItem(window,Note),p,p,w-2*p,Px(46),TRUE);MoveWindow(GetDlgItem(window,List),p,Px(76),w-2*p,std::max(Px(40),h-Px(224)),TRUE);
        const int top=h-Px(140);MoveWindow(GetDlgItem(window,Total),p,top,w-2*p,Px(28),TRUE);
        const int globals[]{All,None,Download,Check,Clear};int x=p;
        for(auto id:globals){if(!GetDlgItem(window,id))continue;MoveWindow(GetDlgItem(window,id),x,top+Px(34),button,bh,TRUE);x+=button+gap;}
        for(int i=0;i<4;++i)MoveWindow(GetDlgItem(window,Action+i),p+i*(button+gap),h-p-bh,button,bh,TRUE);
        MoveWindow(GetDlgItem(window,IDCANCEL),w-p-button,h-p-bh,button,bh,TRUE);
        // 首启只展示选择所需列；管理列按文字最小宽度分配，余量给名称与错误。
        // Onboarding shows selection columns only; manager columns fit measured headers, with spare width for name/error.
        const auto list=GetDlgItem(window,List);RECT client{};GetClientRect(list,&client);
        const int minimum[]{150,90,96,65,96,60,130};int widths[7]{},sum{};
        const auto dc=GetDC(list);const auto old=SelectObject(dc,font);
        for(int i=0;i<(onboarding?4:7);++i){wchar_t text[128]{};LVCOLUMNW col{};col.mask=LVCF_TEXT;col.pszText=text;col.cchTextMax=128;ListView_GetColumn(list,i,&col);SIZE extent{};GetTextExtentPoint32W(dc,text,lstrlenW(text),&extent);widths[i]=std::max(Px(minimum[i]),static_cast<int>(extent.cx)+Px(20));sum+=widths[i];}
        SelectObject(dc,old);ReleaseDC(list,dc);
        const int spare=std::max(0,static_cast<int>(client.right)-sum-1);widths[0]+=onboarding?spare:spare/2;if(!onboarding)widths[6]+=spare-spare/2;
        for(int i=0;i<7;++i)ListView_SetColumnWidth(list,i,widths[i]);
    }
};
void Button(HWND window,int id,const std::wstring& text){const auto child=CreateWindowExW(0,L"BUTTON",text.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SetWindowSubclass(child,ButtonProcedure,1,0);}
INT_PTR CALLBACK Procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto* d=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG){d=reinterpret_cast<Dialog*>(l);SetWindowLongPtrW(window,DWLP_USER,l);d->dpi=GetDpiForWindow(window);d->background=CreateSolidBrush(Color(d->theme.background));
        SetWindowTextW(window,Tr(d->onboarding?"resources.first_run":"resources.manage").c_str());const BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        const auto caption=Color(d->theme.sidebar),text=Color(d->theme.primaryText);DwmSetWindowAttribute(window,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));DwmSetWindowAttribute(window,DWMWA_TEXT_COLOR,&text,sizeof(text));
        const auto instance=GetModuleHandleW(nullptr);for(int id:{Note,Total})CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_NOPREFIX,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
        const auto list=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(List)),instance,nullptr);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|(d->onboarding?LVS_EX_CHECKBOXES:0));SetWindowTheme(list,L"DarkMode_Explorer",nullptr);
        ListView_SetBkColor(list,Color(d->theme.surface));ListView_SetTextBkColor(list,Color(d->theme.surface));ListView_SetTextColor(list,Color(d->theme.primaryText));
        const char* keys[]{"resources.name","resources.state","resources.download_size","resources.progress","resources.installed_size","resources.version","resources.error"};
        for(int i=0;i<7;++i){auto name=Tr(keys[i]);LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=name.data();col.cx=d->Px(140);ListView_InsertColumn(list,i,&col);}
        if(d->onboarding){Button(window,All,Tr("resources.all"));Button(window,None,Tr("resources.none"));}else{Button(window,Check,Tr("resources.check"));Button(window,Clear,Tr("resources.clear_cache"));}
        Button(window,Download,Tr(d->onboarding?"resources.download":"resources.download_all"));Button(window,IDCANCEL,Tr(d->onboarding?"resources.later":"dialog.ok"));for(int i=0;i<4;++i)Button(window,Action+i,L"");
        d->Font(window);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(GetWindow(window,GW_OWNER),MONITOR_DEFAULTTONEAREST),&monitor);
        RECT owner=monitor.rcWork;if(GetWindow(window,GW_OWNER))GetWindowRect(GetWindow(window,GW_OWNER),&owner);
        const int width=std::min<LONG>(d->Px(850),monitor.rcWork.right-monitor.rcWork.left),height=std::min<LONG>(d->Px(610),monitor.rcWork.bottom-monitor.rcWork.top);
        SetWindowPos(window,nullptr,std::clamp((owner.left+owner.right-width)/2,monitor.rcWork.left,monitor.rcWork.right-width),std::clamp((owner.top+owner.bottom-height)/2,monitor.rcWork.top,monitor.rcWork.bottom-height),width,height,SWP_NOZORDER);
        d->Refresh(window);d->Layout(window);SetTimer(window,1,500,nullptr);SetFocus(GetDlgItem(window,IDCANCEL));return FALSE;
    }
    if(!d)return FALSE;
    switch(message){
    case WM_TIMER:d->Refresh(window);return TRUE;
    case WM_SIZE:d->Layout(window);return TRUE;
    case WM_DPICHANGED:{d->dpi=HIWORD(w);d->Font(window);const auto* r=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER);d->Layout(window);return TRUE;}
    case WM_CLOSE:EndDialog(window,IDCANCEL);return TRUE;
    case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={d->Px(720),d->Px(430)};return TRUE;}
    case WM_NOTIFY:{const auto* header=reinterpret_cast<NMHDR*>(l);if(header->idFrom==List&&header->code==LVN_ITEMCHANGED&&!d->updating){const auto* item=reinterpret_cast<NMLISTVIEW*>(l);
        if(item->iItem>=0&&static_cast<std::size_t>(item->iItem)<d->rows.size()){if(item->uNewState&LVIS_SELECTED)d->selected=item->iItem;if(d->onboarding)d->selection.Toggle(d->rows[static_cast<std::size_t>(item->iItem)],ListView_GetCheckState(header->hwndFrom,item->iItem)!=FALSE);}d->Refresh(window);}return TRUE;}
    case WM_COMMAND:{const auto id=LOWORD(w);if(id==IDCANCEL){EndDialog(window,IDCANCEL);return TRUE;}
        if(id==All)d->selection.All(d->rows);if(id==None)d->selection.None();
        if(id==Download){if(d->onboarding){d->decision=d->selection.Download(d->rows,d->service.CanDownload());if(d->decision.download)EndDialog(window,IDOK);}else if(d->service.CanDownload())d->service.DownloadAll();}
        if(id==Check)d->service.CheckResources();if(id==Clear&&!d->service.ClearDownloadCache())(void)ShowMessageDialog(window,Tr("resources.manage"),Tr("resources.busy"),MessageKind::Information,Tr("dialog.ok"));
        if(id>=Action&&id<Action+4&&d->selected>=0&&static_cast<std::size_t>(id-Action)<d->actions.size()){
            const auto action=d->actions[static_cast<std::size_t>(id-Action)];bool accepted=true;
            if(action==resources::ResourceAction::Delete)accepted=ShowMessageDialog(window,Tr("resources.delete"),Tr("resources.delete_confirm"),MessageKind::Warning,Tr("resources.delete"),Tr("dialog.cancel"));
            if(accepted&&!d->service.Act(d->rows[static_cast<std::size_t>(d->selected)].record.resourceId,action))(void)ShowMessageDialog(window,Tr("resources.manage"),Tr("resources.busy"),MessageKind::Information,Tr("dialog.ok"));
        }d->Refresh(window);return TRUE;}
    case WM_CTLCOLORSTATIC:SetBkColor(reinterpret_cast<HDC>(w),Color(d->theme.background));SetTextColor(reinterpret_cast<HDC>(w),Color(d->theme.primaryText));return reinterpret_cast<INT_PTR>(d->background);
    case WM_ERASEBKGND:{RECT r{};GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(w),&r,d->background);return TRUE;}
    case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);const auto brush=CreateSolidBrush(Color(item.itemState&ODS_SELECTED?d->theme.hover:d->theme.selected));FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);
        SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,Color(item.itemState&ODS_DISABLED?d->theme.secondaryText:d->theme.accent));wchar_t text[256]{};GetWindowTextW(item.hwndItem,text,256);auto rect=item.rcItem;DrawTextW(item.hDC,text,-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&rect);return TRUE;}
    }return FALSE;
}
}
ResourceDialogDecision ShowResourceDialog(HWND owner,resources::ResourceService& service,const std::filesystem::path& root,bool onboarding){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};if(!InitCommonControlsEx(&controls))return {};
    Dialog dialog{service,root,onboarding};struct Template{DLGTEMPLATE header;WORD menu{},windowClass{},title{};} spec{};spec.header.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|DS_MODALFRAME;
    const auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&spec.header,owner,Procedure,reinterpret_cast<LPARAM>(&dialog));return result==IDOK?dialog.decision:ResourceDialogDecision{};
}
}
