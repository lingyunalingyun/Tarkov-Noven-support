#pragma once
#include "data/AppSettings.h"
#include "ui/NavigationButton.h"
#include "ui/localization/LocalizationService.h"
#include <functional>
#include <shobjidl.h>

namespace noven::ui {
inline std::wstring ScanShortcutText(UINT key,UINT modifiers) {
    std::wstring text;
    if(modifiers&MOD_CONTROL)text+=L"Ctrl+";
    if(modifiers&MOD_ALT)text+=L"Alt+";
    if(modifiers&MOD_SHIFT)text+=L"Shift+";
    if(modifiers&MOD_WIN)text+=L"Win+";
    if(key>=VK_F1&&key<=VK_F24)return text+L"F"+std::to_wstring(key-VK_F1+1);
    return text+static_cast<wchar_t>(key);
}
// 设置控件只提供用户选择，注册热键、持久化及日志服务归应用层。
// Settings controls collect user choices; hotkey registration, storage and log services belong to the app.
class PreferencesPanel final {
public:
    data::AppSettings value;
    std::function<bool(const data::AppSettings&)> changed;
    std::function<void()> resources;
    bool Recording() const noexcept {return recording_;}
    void Blur(){recording_=false;pressed_.reset();}
    void Draw(const UiCanvas& canvas,const UiTheme& theme,float left,float right) {
        directory_=D2D1::RectF(left,314,right,354);hotkey_=D2D1::RectF(left,442,right,482);
        canvas.Text(Tr("settings.game_directory"),canvas.label,D2D1::RectF(left,276,right,306),theme.primaryText);
        DrawTextButton(canvas,theme,directory_,Tr("settings.choose_directory"),false,pressed_==0);
        const auto path=value.gameDirectory.empty()?Tr("settings.directory_unset"):value.gameDirectory.wstring();
        canvas.Text(path,canvas.smallFormat,D2D1::RectF(left+12,360,right-12,389),theme.secondaryText);
        canvas.Text(Tr("settings.scan_shortcut"),canvas.label,D2D1::RectF(left,404,right,434),theme.primaryText);
        DrawTextButton(canvas,theme,hotkey_,recording_?Tr("settings.shortcut_recording"):ScanShortcutText(value.scanKey,value.scanModifiers),false,pressed_==1);
        canvas.Text(Tr(error_.empty()?"settings.shortcut_hint":error_),canvas.smallFormat,D2D1::RectF(left,492,right,524),theme.secondaryText);
        resource_=D2D1::RectF(left,550,right,594);
        DrawTextButton(canvas,theme,resource_,Tr("resources.manage"),false,pressed_==2);
    }
    bool Down(float x,float y) {
        pressed_.reset();
        if(HitNavigationButton(directory_,x,y))pressed_=0;
        if(HitNavigationButton(hotkey_,x,y))pressed_=1;
        if(resources&&HitNavigationButton(resource_,x,y))pressed_=2;
        return pressed_.has_value();
    }
    bool Up(HWND window,float x,float y) {
        const auto pressed=pressed_;pressed_.reset();if(!pressed)return false;
        if(*pressed==2){if(resources&&HitNavigationButton(resource_,x,y))resources();return true;}
        if(*pressed==1&&HitNavigationButton(hotkey_,x,y)){recording_=!recording_;error_.clear();return true;}
        if(*pressed!=0||!HitNavigationButton(directory_,x,y))return true;
        const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(FAILED(initialized))return true;
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
        if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) {
            dialog->SetOptions(FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);
            const auto title=Tr("settings.choose_directory");dialog->SetTitle(title.c_str());
            if(SUCCEEDED(dialog->Show(window))) {
                Microsoft::WRL::ComPtr<IShellItem> item;PWSTR path{};
                if(SUCCEEDED(dialog->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
                    auto next=value;next.gameDirectory=path;CoTaskMemFree(path);
                    if(!data::GameLogRoot(next.gameDirectory))error_="settings.directory_invalid";
                    else if(changed&&changed(next)){value=next;error_.clear();}
                    else error_="settings.save_failed";
                }
            }
        }
        dialog.Reset();CoUninitialize();return true;
    }
    bool Key(UINT key,UINT modifiers) {
        if(!recording_)return false;
        if(key==VK_ESCAPE){recording_=false;return true;}
        if(!data::AppSettings::ValidKey(key))return true;
        auto next=value;next.scanKey=key;next.scanModifiers=modifiers;
        if(changed&&changed(next)){value=next;recording_=false;error_.clear();}
        else error_="settings.shortcut_failed";
        return true;
    }
private:
    D2D1_RECT_F directory_{},hotkey_{},resource_{};
    std::optional<int> pressed_;
    bool recording_{};
    std::string error_;
};
}
