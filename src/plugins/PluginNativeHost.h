#pragma once
#include "common/AppPaths.h"
#include "plugins/PluginNativePath.h"
#include "plugins/PluginUiDocument.h"
#include "plugins/PluginHttpService.h"
#include "noven_plugin_abi_v1.h"
#include <map>
#include <deque>

namespace noven::plugins {
// 本编译单元只链接进 Host，绝不链接到 Noven 主程序；进程边界不是 OS 沙箱。
// Linked into Host only, never the main application; process isolation is not an OS sandbox.
class NativePluginHost final {
public:
    explicit NativePluginHost(std::filesystem::path executableDirectory,std::unique_ptr<IPluginHttpBackend> testBackend={}):NativePluginHost(common::AppPaths::Development(std::filesystem::absolute(executableDirectory)),std::move(testBackend)){}
    explicit NativePluginHost(const common::AppPaths& paths,std::unique_ptr<IPluginHttpBackend> testBackend={}):root_(paths.Plugins()),testBackend_(std::move(testBackend)){}
    ~NativePluginHost();
    bool Message(const ipc::Message& message,ipc::Channel& channel,HANDLE parent);
    void Shutdown();
    HANDLE Wake() const {return http_?http_->Wake():nullptr;}
    bool Pump();
private:
    static int32_t NOVEN_CALL Log(void*,NovenUtf8V1);
    static int32_t NOVEN_CALL Register(void*,NovenUtf8V1,NovenUtf8V1);
    static int32_t NOVEN_CALL Publish(void*,NovenUtf8V1,NovenUtf8V1);
    static int32_t NOVEN_CALL RequestData(void*,const NovenDataRequestV1*);
    static int32_t NOVEN_CALL RequestStorage(void*,const NovenStorageRequestV1*);
    static int32_t NOVEN_CALL RequestHttp(void*,const NovenHttpRequestV1*);
    static int32_t NOVEN_CALL Subscribe(void*);
    static int32_t NOVEN_CALL Unsubscribe(void*);
    int32_t SetSubscription(bool enabled);
    bool FlushData();
    int32_t Send(ipc::Message message);
    int Load(const ipc::Message& message);
    std::filesystem::path root_;
    std::optional<NativeFile> file_;
    HMODULE library_{};
    ipc::Channel* channel_{};
    HANDLE parent_{};
    DWORD thread_{GetCurrentThreadId()};
    NovenHostApiV1 host_{sizeof(NovenHostApiV1),NOVEN_PLUGIN_API_VERSION,this,&Log,&Register,&Publish};
    NovenPluginInstanceV1 instance_{};
    NovenCatalogHostApiV1 catalogHost_{sizeof(NovenCatalogHostApiV1),NOVEN_CATALOG_SCHEMA_VERSION,this,&RequestData};
    NovenCatalogInstanceV1 catalogInstance_{};
    NovenScanHostApiV1 scanHost_{sizeof(NovenScanHostApiV1),NOVEN_SCAN_SCHEMA_VERSION,this,&Subscribe,&Unsubscribe};
    NovenScanInstanceV1 scanInstance_{};
    NovenStorageHostApiV1 storageHost_{sizeof(NovenStorageHostApiV1),NOVEN_STORAGE_SCHEMA_VERSION,this,&RequestStorage};
    NovenStorageInstanceV1 storageInstance_{};
    NovenHttpHostApiV1 httpHost_{sizeof(NovenHttpHostApiV1),NOVEN_HTTP_SCHEMA_VERSION,this,&RequestHttp};
    NovenHttpInstanceV1 httpInstance_{};
    std::unique_ptr<PluginHttpService> http_;
    std::unique_ptr<IPluginHttpBackend> testBackend_;
    bool httpConfigured_{};
    bool storageConfigured_{},storagePermission_{},storageInFlight_{};
    DataRequestBudget storageBudget_;
    std::deque<StorageRequest> storageQueue_;
    std::optional<bool> scanCommand_;
    bool scanConfigured_{},scanPermission_{},scanSubscribed_{},scanAwaiting_{};
    std::uint64_t lastScanSequence_{};
    DataRequestBudget dataBudget_;
    std::deque<DataRequest> dataQueue_;
    bool catalogConfigured_{};
    unsigned catalogMask_{};
    bool attempted_{},initialized_{},pagePermission_{},accepting_{};
    std::map<std::string,UiDocument,std::less<>> pages_;
    std::chrono::steady_clock::time_point burst_{std::chrono::steady_clock::now()};
    unsigned logs_{},updates_{};
};
}
