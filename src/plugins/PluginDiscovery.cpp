#include "plugins/PluginDiscovery.h"
#include <algorithm>
#include <map>

namespace noven::plugins {
namespace {
struct Handle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;
    explicit Handle(HANDLE handle):value(handle){}
};
Handle Open(const std::filesystem::path& path,bool directory) {
    return Handle{CreateFileW(path.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr)};
}
std::optional<std::filesystem::path> FinalPath(HANDLE handle) {
    const auto length=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED);
    if(!length||length>32768)return {};
    std::wstring text(length,L'\0');const auto used=GetFinalPathNameByHandleW(handle,text.data(),length,FILE_NAME_NORMALIZED);
    if(!used||used>=length)return {};text.resize(used);return std::filesystem::path(text).lexically_normal();
}
bool EqualPath(const std::filesystem::path& a,const std::filesystem::path& b) {
    const auto& x=a.native();const auto& y=b.native();
    return CompareStringOrdinal(x.data(),static_cast<int>(x.size()),y.data(),static_cast<int>(y.size()),TRUE)==CSTR_EQUAL;
}
bool SafeHandle(HANDLE handle,bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    return handle!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(handle,&info)&&!UnsafeAttributes(info.dwFileAttributes,directory)
        &&(directory||info.nNumberOfLinks==1);
}
PluginRecord ReadDirectory(const std::filesystem::path& directory,const std::filesystem::path& root) {
    PluginRecord record;record.directory=directory;
    const auto fail=[&](PluginState state,const char* key){record.state=state;record.diagnostics.push_back({key,{}});return record;};
    // 打开重解析点本身并验证最终路径；目录句柄禁止替换，文件句柄禁止并发写入/替换。
    // Open reparse points themselves and verify final paths; directory/file handles deny replacement and concurrent file writes.
    auto child=Open(directory,true);const auto childPath=FinalPath(child.value);
    if(!SafeHandle(child.value,true)||!childPath||!EqualPath(childPath->parent_path(),root))return fail(PluginState::UnsafePath,"plugins.diag.path");
    auto file=Open(directory/L"manifest.json",false);
    if(file.value==INVALID_HANDLE_VALUE)return fail(PluginState::InvalidManifest,GetLastError()==ERROR_FILE_NOT_FOUND?"plugins.diag.missing":"plugins.diag.read");
    const auto filePath=FinalPath(file.value);
    if(!SafeHandle(file.value,false)||!filePath||!EqualPath(filePath->parent_path(),*childPath))return fail(PluginState::UnsafePath,"plugins.diag.path");
    LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size)||size.QuadPart<0)return fail(PluginState::InvalidManifest,"plugins.diag.read");
    if(size.QuadPart>static_cast<LONGLONG>(MaximumManifestBytes))return fail(PluginState::InvalidManifest,"plugins.diag.size");
    std::string text(static_cast<std::size_t>(size.QuadPart),'\0');DWORD read{};
    if(!ReadFile(file.value,text.data(),static_cast<DWORD>(text.size()),&read,nullptr)||read!=text.size())return fail(PluginState::InvalidManifest,"plugins.diag.read");
    auto parsed=ParseManifest(text);record.state=parsed.state;record.manifest=std::move(parsed.manifest);record.diagnostics=std::move(parsed.diagnostics);return record;
}
PluginSnapshot Discover(const std::filesystem::path& root) {
    PluginSnapshot snapshot;
    const auto fail=[&](const char* key){snapshot.records.clear();snapshot.diagnostics.push_back({key,{}});return snapshot;};
    auto rootHandle=Open(root,true);
    if(rootHandle.value==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return snapshot;
        return fail("plugins.diag.root");
    }
    const auto finalRoot=FinalPath(rootHandle.value);
    if(!SafeHandle(rootHandle.value,true)||!finalRoot)return fail("plugins.diag.path");
    WIN32_FIND_DATAW entry{};const auto search=FindFirstFileW((root/L"*").c_str(),&entry);
    if(search==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?snapshot:fail("plugins.diag.root");
    struct Search final {HANDLE value;~Search(){FindClose(value);}} searchHandle{search};
    std::vector<std::filesystem::path> directories;std::size_t entries{};
    do {
        const std::wstring_view name(entry.cFileName);if(name==L"."||name==L"..")continue;
        // 整体超限返回显式诊断，不从枚举顺序中任意挑选“赢家”；最多读取 128 个清单。
        // Fail explicitly on capacity, never choose arbitrary enumeration winners; read at most 128 manifests.
        if(++entries>1024)return fail("plugins.diag.limit");
        if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
            directories.push_back(root/entry.cFileName);if(directories.size()>128)return fail("plugins.diag.limit");
        }
    } while(FindNextFileW(search,&entry));
    if(GetLastError()!=ERROR_NO_MORE_FILES)return fail("plugins.diag.root");
    std::sort(directories.begin(),directories.end());
    for(const auto& path:directories)snapshot.records.push_back(ReadDirectory(path,*finalRoot));
    std::map<std::string,std::size_t> counts;
    for(const auto& record:snapshot.records)if(record.manifest)++counts[record.manifest->id];
    for(auto& record:snapshot.records)if(record.manifest&&counts[record.manifest->id]>1) {
        record.state=PluginState::DuplicateId;record.diagnostics.push_back({"plugins.diag.duplicate","id"});
    }
    return snapshot;
}
}
const PluginSnapshot& PluginDiscovery::Refresh() {
    ++refreshCount_;
    try {snapshot_=Discover(root_);}catch(const std::exception&){snapshot_={};snapshot_.diagnostics.push_back({"plugins.diag.root",{}});}
    return snapshot_;
}
}
