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
void NativePluginHost::Shutdown(){accepting_=false;dataQueue_.clear();dataBudget_.Clear();if(initialized_){initialized_=false;instance_.shutdown(instance_.context);}}
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
int32_t NOVEN_CALL NativePluginHost::RequestData(void* context,const NovenDataRequestV1* input) try {
    auto& self=*static_cast<NativePluginHost*>(context);
    if(GetCurrentThreadId()!=self.thread_||!self.accepting_)return NOVEN_ERROR_STATE;
    if(!input||input->struct_size<sizeof(NovenDataRequestV1))return NOVEN_ERROR_ARGUMENT;
    DataRequest request;request.requestId=input->request_id;request.catalog=static_cast<CatalogKind>(input->catalog_kind);
    request.operation=static_cast<DataOperation>(input->operation);request.offset=input->offset;request.limit=input->limit;
    if(input->stable_id_utf8.length)request.stableId=Slice(input->stable_id_utf8,128);
    if(!ValidDataRequest(request))return NOVEN_ERROR_ARGUMENT;
    if(!(self.catalogMask_&(1u<<(static_cast<unsigned>(request.catalog)-1))))return NOVEN_ERROR_PERMISSION;
    const auto admission=self.dataBudget_.Begin(request.requestId);
    if(admission==RequestAdmission::Duplicate)return NOVEN_ERROR_STATE;
    if(admission==RequestAdmission::Limited)return NOVEN_ERROR_LIMIT;
    self.dataQueue_.push_back(std::move(request));return NOVEN_OK;
}catch(const std::exception&){return NOVEN_ERROR_ARGUMENT;}
bool NativePluginHost::FlushData(){
    // 插件回调已返回才写 IPC；请求等待绝不阻塞插件回调或主 UI 线程。
    // Write IPC only after plugin callbacks return, never waiting inside callbacks or the main UI thread.
    while(!dataQueue_.empty()){
        ipc::Message message{ipc::MessageType::DataRequest};message.dataRequest=std::move(dataQueue_.front());dataQueue_.pop_front();
        if(Send(std::move(message))!=NOVEN_OK)return false;
    }
    return true;
}
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
    initialized_=true;
    const auto catalogInitialize=reinterpret_cast<NovenInitializeCatalogFn>(GetProcAddress(library_,"NovenPlugin_InitializeCatalogV1"));
    if(catalogInitialize){
        catalogInstance_={sizeof(NovenCatalogInstanceV1),NOVEN_CATALOG_SCHEMA_VERSION,nullptr};
        if(catalogInitialize(&catalogHost_,&catalogInstance_)!=NOVEN_OK)return 6;
        if(catalogInstance_.struct_size!=sizeof(catalogInstance_)||catalogInstance_.schema_version!=NOVEN_CATALOG_SCHEMA_VERSION||!catalogInstance_.on_data_result)return 5;
    }
    return 0;
}
bool NativePluginHost::Message(const ipc::Message& message,ipc::Channel& channel,HANDLE parent) {
    channel_=&channel;parent_=parent;
    if(message.type==ipc::MessageType::CatalogAccess){
        if(attempted_||catalogConfigured_)return false;
        catalogConfigured_=true;catalogMask_=message.catalogMask;return true;
    }
    if(message.type==ipc::MessageType::LoadPlugin) {
        ipc::Message reply{ipc::MessageType::LoadPluginResult};reply.result=Load(message);
        if(Send(reply)!=NOVEN_OK)return false;
        if(reply.result){accepting_=false;dataQueue_.clear();dataBudget_.Clear();return true;}
        return FlushData();
    }
    if(message.type==ipc::MessageType::DataResult){
        if(!initialized_||!accepting_||!catalogInstance_.on_data_result||!dataBudget_.Complete(message.dataResult.requestId))return false;
        const NovenDataResultV1 result{sizeof(NovenDataResultV1),static_cast<std::uint32_t>(message.dataResult.status),message.dataResult.requestId,Borrow(message.dataResult.payload)};
        catalogInstance_.on_data_result(instance_.context,&result);
        ipc::Message ack{ipc::MessageType::DataResultAck};ack.dataResult.requestId=result.request_id;
        return Send(ack)==NOVEN_OK&&FlushData();
    }
    if(message.type==ipc::MessageType::UiAction) {
        if(!initialized_||!accepting_||!pagePermission_)return false;
        const auto found=pages_.find(message.pageId);if(found==pages_.end()||!found->second.HasAction(message.actionId))return false;
        ipc::Message reply{ipc::MessageType::UiActionResult};reply.result=instance_.on_ui_action(instance_.context,Borrow(message.pageId),Borrow(message.actionId))==NOVEN_OK?0:6;
        return Send(reply)==NOVEN_OK&&FlushData();
    }
    return false;
}
}
