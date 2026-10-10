#pragma once
#include <windows.h>
namespace noven::common {
inline bool InstallationInProgress(){
    const auto marker=OpenMutexW(SYNCHRONIZE,FALSE,L"Local\\NovenTarkovSupport.Installer.70C934D2");
    if(marker){CloseHandle(marker);return true;}return GetLastError()!=ERROR_FILE_NOT_FOUND;
}
// 固定名称供安装器检查；不终止其他进程，多个 Host 可持有同一存在性标记。
// Stable names let Setup check ownership without killing processes; multiple Hosts may hold one marker.
struct RuntimeOwnership final {
    HANDLE handle;
    explicit RuntimeOwnership(bool host):handle(CreateMutexW(nullptr,FALSE,host?L"Local\\NovenTarkovSupport.Host.70C934D2":L"Local\\NovenTarkovSupport.App.70C934D2")){
        if(handle&&InstallationInProgress()){CloseHandle(handle);handle=nullptr;}
    }
    ~RuntimeOwnership(){if(handle)CloseHandle(handle);}
    RuntimeOwnership(const RuntimeOwnership&)=delete;
};
}
