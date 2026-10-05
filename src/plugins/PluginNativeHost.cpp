#include "plugins/PluginNativeHost.h"
#include <stdexcept>

namespace noven::plugins {
namespace {
std::string_view Slice(NovenUtf8V1 text,std::size_t maximum) {
    if(!text.data||text.length==0||text.length>maximum)throw std::runtime_error("invalid ABI text");
    return {text.data,text.length};
}
NovenUtf8V1 Borrow(const std::string& text){return {text.data(),static_cast<std::uint32_t>(text.size())};}
}
NativePluginHost::~NativePluginHost(){Shutdown();if(library_)FreeLibrary(library_);}
void NativePluginHost::Shutdown(){accepting_=false;if(initialized_){initialized_=false;instance_.shutdown(instance_.context);}}
int32_t NativePluginHost::Send(ipc::Message message) {
    if(GetCurrentThreadId()!=thread_||!channel_)return NOVEN_ERROR_STATE;
    return channel_->Write(message,ipc::After(2000),parent_)==ipc::IoResult::Complete?NOVEN_OK:NOVEN_ERROR_TRANSPORT;
}
int32_t NOVEN_CALL NativePluginHost::Log(void* context,NovenUtf8V1 text) try {
    auto& self=*static_cast<NativePluginHost*>(context);if(GetCurrentThreadId()!=self.thread_)return NOVEN_ERROR_STATE;
    const auto value=Slice(text,1024);if(!ValidUiText(value,1024))return NOVEN_ERROR_ARGUMENT;
    const auto now=std::chrono::steady_clock::now();if(now-self.burst_>=std::chrono::seconds(1)){self.burst_=now;self.logs_=self.updates_=0;}
    if(self.logs_++>=16)return NOVEN_ERROR_LIMIT;
    ipc::Message message{ipc::MessageType::Log};message.text=value;return self.Send(std::move(message));
}catch(const std::exception&){return NOVEN_ERROR_ARGUMENT;}
int32_t NOVEN_CALL NativePluginHost::Register(void* context,NovenUtf8V1 localId,NovenUtf8V1 title) try {
    auto& self=*static_cast<NativePluginHost*>(context);if(GetCurrentThreadId()!=self.thread_||!self.accepting_)return NOVEN_ERROR_STATE;
    if(!self.pagePermission_)return NOVEN_ERROR_PERMISSION;
    const auto id=Slice(localId,64),label=Slice(title,256);if(!ValidLocalId(id)||!ValidUiText(label,256))return NOVEN_ERROR_ARGUMENT;
    if(self.pages_.contains(id))return NOVEN_ERROR_STATE;if(self.pages_.size()>=MaximumPluginPages)return NOVEN_ERROR_LIMIT;
    ipc::Message message{ipc::MessageType::UiRegisterPage};message.pageId=id;message.title=label;
    const auto result=self.Send(message);if(result==NOVEN_OK)self.pages_.emplace(std::string(id),UiDocument{});return result;
}catch(const std::exception&){return NOVEN_ERROR_ARGUMENT;}
int32_t NOVEN_CALL NativePluginHost::Publish(void* context,NovenUtf8V1 localId,NovenUtf8V1 json) try {
    auto& self=*static_cast<NativePluginHost*>(context);if(GetCurrentThreadId()!=self.thread_||!self.accepting_)return NOVEN_ERROR_STATE;
    if(!self.pagePermission_)return NOVEN_ERROR_PERMISSION;
    const auto id=Slice(localId,64),text=Slice(json,MaximumDocumentBytes);if(!ValidLocalId(id))return NOVEN_ERROR_ARGUMENT;
    const auto found=self.pages_.find(id);if(found==self.pages_.end())return NOVEN_ERROR_STATE;
    auto document=ParseUiDocument(text);const auto now=std::chrono::steady_clock::now();
    if(now-self.burst_>=std::chrono::seconds(1)){self.burst_=now;self.logs_=self.updates_=0;}
    if(self.updates_++>=32)return NOVEN_ERROR_LIMIT;
    ipc::Message message{ipc::MessageType::UiPublishPage};message.pageId=id;message.document=text;
    const auto result=self.Send(message);if(result==NOVEN_OK)found->second=std::move(document);return result;
}catch(const std::exception&){return NOVEN_ERROR_ARGUMENT;}
int NativePluginHost::Load(const ipc::Message& message) {
    if(attempted_)return 6;attempted_=true;pagePermission_=message.pagePermission;
    try{file_=NativeFile::Open(root_,std::filesystem::path(std::u8string(message.directory.begin(),message.directory.end())),message.entry);}catch(const std::exception&){return 1;}
    // 绝对路径 + DLL 所在目录/System32，排除 CWD、PATH 与插件指定搜索目录。
    // Absolute path + DLL directory/System32 exclude CWD, PATH and plugin-selected search directories.
    library_=LoadLibraryExW(file_->path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!library_)return 2;
    const auto abi=reinterpret_cast<NovenGetAbiVersionFn>(GetProcAddress(library_,"NovenPlugin_GetAbiVersion"));
    const auto initialize=reinterpret_cast<NovenInitializeFn>(GetProcAddress(library_,"NovenPlugin_Initialize"));
    if(!abi||!initialize)return 3;if(abi()!=NOVEN_NATIVE_ABI_VERSION)return 4;
    instance_={sizeof(NovenPluginInstanceV1),NOVEN_NATIVE_ABI_VERSION,nullptr,nullptr,nullptr};accepting_=true;
    if(initialize(&host_,&instance_)!=NOVEN_OK){accepting_=false;return 6;}
    if(instance_.struct_size!=sizeof(instance_)||instance_.abi_version!=NOVEN_NATIVE_ABI_VERSION||!instance_.shutdown||!instance_.on_ui_action){accepting_=false;return 5;}
    initialized_=true;return 0;
}
bool NativePluginHost::Message(const ipc::Message& message,ipc::Channel& channel,HANDLE parent) {
    channel_=&channel;parent_=parent;
    if(message.type==ipc::MessageType::LoadPlugin) {
        ipc::Message reply{ipc::MessageType::LoadPluginResult};reply.result=Load(message);Send(reply);return true;
    }
    if(message.type==ipc::MessageType::UiAction) {
        if(!initialized_||!accepting_||!pagePermission_)return false;
        const auto found=pages_.find(message.pageId);if(found==pages_.end()||!found->second.HasAction(message.actionId))return false;
        ipc::Message reply{ipc::MessageType::UiActionResult};reply.result=instance_.on_ui_action(instance_.context,Borrow(message.pageId),Borrow(message.actionId))==NOVEN_OK?0:6;
        Send(reply);return true;
    }
    return false;
}
}
