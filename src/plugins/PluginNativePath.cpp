#include "plugins/PluginNativePath.h"
#include "plugins/PluginManifest.h"
#include <stdexcept>

namespace noven::plugins {
namespace {
ipc::Handle OpenChecked(const std::filesystem::path& path,bool directory) {
    ipc::Handle handle(CreateFileW(path.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if(!handle||!GetFileInformationByHandle(handle.Get(),&info)||GetFileType(handle.Get())!=FILE_TYPE_DISK
        ||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        ||((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory||(!directory&&info.nNumberOfLinks!=1))
        throw std::runtime_error("unsafe native plugin path");
    return handle;
}
std::filesystem::path Final(HANDLE handle) {
    std::wstring path(32768,L'\0');const auto used=GetFinalPathNameByHandleW(handle,path.data(),static_cast<DWORD>(path.size()),FILE_NAME_NORMALIZED);
    if(!used||used>=path.size())throw std::runtime_error("native path resolution failed");
    path.resize(used);return std::filesystem::path(path).lexically_normal();
}
bool Equal(const std::filesystem::path& a,const std::filesystem::path& b) {
    return CompareStringOrdinal(a.c_str(),static_cast<int>(a.native().size()),b.c_str(),static_cast<int>(b.native().size()),TRUE)==CSTR_EQUAL;
}
}
NativeFile NativeFile::Open(const std::filesystem::path& rootPath,const std::filesystem::path& directoryPath,std::string_view entry) {
    if(!rootPath.is_absolute()||!directoryPath.is_absolute()||!ValidRuntimeEntry(entry))throw std::runtime_error("invalid native plugin entry");
    NativeFile result;result.root=OpenChecked(rootPath,true);result.directory=OpenChecked(directoryPath,true);
    const auto root=Final(result.root.Get()),directory=Final(result.directory.Get());
    if(!Equal(directory.parent_path(),root))throw std::runtime_error("native plugin outside root");
    const std::filesystem::path name{std::u8string(entry.begin(),entry.end())};
    result.file=OpenChecked(directory/name,false);result.path=Final(result.file.Get());
    if(!Equal(result.path.parent_path(),directory)||!Equal(result.path.filename(),name))throw std::runtime_error("native entry outside plugin directory");
    // 扩展名不能伪装 EXE：只读检查 PE 的 DLL 位，不在主程序中映射任何代码。
    // Extensions cannot disguise EXEs: inspect PE DLL bit as bytes, never map code in the main process.
    IMAGE_DOS_HEADER dos{};DWORD read{};
    if(!ReadFile(result.file.Get(),&dos,sizeof(dos),&read,nullptr)||read!=sizeof(dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>1024*1024)
        throw std::runtime_error("invalid native DLL image");
    LARGE_INTEGER offset{};offset.QuadPart=dos.e_lfanew;
    DWORD signature{};IMAGE_FILE_HEADER header{};
    if(!SetFilePointerEx(result.file.Get(),offset,nullptr,FILE_BEGIN)||!ReadFile(result.file.Get(),&signature,sizeof(signature),&read,nullptr)||read!=sizeof(signature)||signature!=IMAGE_NT_SIGNATURE
        ||!ReadFile(result.file.Get(),&header,sizeof(header),&read,nullptr)||read!=sizeof(header)||!(header.Characteristics&IMAGE_FILE_DLL)||header.Machine!=IMAGE_FILE_MACHINE_AMD64)
        throw std::runtime_error("incompatible native DLL image");
    return result;
}
}
