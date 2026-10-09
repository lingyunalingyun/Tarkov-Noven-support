#include "app/App.h"
#include "common/RuntimeOwnership.h"
#include "ui/MessageDialog.h"
#include <exception>

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show_command
) {
    noven::common::RuntimeOwnership ownership(false);
    if(!ownership.handle)return 1;
    try {noven::App app;return app.Run(instance, show_command);}
    catch(const std::exception&){
        noven::ui::ShowMessageDialog(nullptr,L"Noven Tarkov Support",L"无法初始化程序或用户数据目录。请检查当前用户的 LocalAppData。\nCannot initialize application or user data paths. Check this user's LocalAppData.",noven::ui::MessageKind::Error,L"OK");return 1;
    }
}
