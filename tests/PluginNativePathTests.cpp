#include "plugins/PluginNativePath.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-path-test-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
void Image(const std::filesystem::path& path,bool dll=true,WORD machine=IMAGE_FILE_MACHINE_AMD64) {
    IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=sizeof(dos);const DWORD signature=IMAGE_NT_SIGNATURE;
    IMAGE_FILE_HEADER header{};header.Machine=machine;header.Characteristics=dll?IMAGE_FILE_DLL:IMAGE_FILE_EXECUTABLE_IMAGE;
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(&dos),sizeof(dos));file.write(reinterpret_cast<const char*>(&signature),sizeof(signature));file.write(reinterpret_cast<const char*>(&header),sizeof(header));
}
bool Reject(const std::filesystem::path& root,const std::filesystem::path& directory,std::string_view entry) {
    try{auto file=NativeFile::Open(root,directory,entry);return false;}catch(const std::exception&){return true;}
}
int main() try {
    Temp temp;const auto root=temp.path/"plugins",directory=root/"com.example.test";
    std::filesystem::create_directories(directory);Image(directory/"plugin.dll");
    {
        auto file=NativeFile::Open(root,directory,"plugin.dll");Check(file.file&&file.root&&file.directory,"regular direct-child PE DLL checked without loading");
        ipc::Handle writer(CreateFileW((directory/"plugin.dll").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));
        Check(!writer,"DLL cannot be changed while native guard is retained");
        Check(!MoveFileExW(directory.c_str(),(root/"moved").c_str(),0),"directory cannot be replaced during load guard");
    }
    Check(Reject(root,directory,"../plugin.dll")&&Reject(root,directory,"missing.dll"),"traversal/missing file fail closed");
    Image(directory/"exe.dll",false);Image(directory/"x86.dll",true,IMAGE_FILE_MACHINE_I386);
    Check(Reject(root,directory,"exe.dll")&&Reject(root,directory,"x86.dll"),"EXE disguise/wrong architecture rejected as bytes");
    const auto outside=temp.path/"outside";std::filesystem::create_directory(outside);Image(outside/"plugin.dll");
    Check(Reject(root,outside,"plugin.dll"),"directory outside root rejected");
    Check(CreateHardLinkW((directory/"linked.dll").c_str(),(outside/"plugin.dll").c_str(),nullptr)!=0,"hardlink fixture");
    Check(Reject(root,directory,"linked.dll"),"hardlink escape fails closed");
    std::filesystem::create_directory(directory/"folder.dll");Check(Reject(root,directory,"folder.dll"),"directory is not runtime file");
    if(CreateSymbolicLinkW((directory/"symlink.dll").c_str(),(directory/"plugin.dll").c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))Check(Reject(root,directory,"symlink.dll"),"reparse runtime rejected");
    const std::string invalidUtf8=std::string(1,static_cast<char>(0xff))+".dll";Check(Reject(root,directory,invalidUtf8),"invalid UTF8 filename rejected");
    std::cout<<"Native path guards PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
