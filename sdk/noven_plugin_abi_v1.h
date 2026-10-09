#ifndef NOVEN_PLUGIN_ABI_V1_H
#define NOVEN_PLUGIN_ABI_V1_H
#include <stdint.h>

#if defined(_WIN32)
#define NOVEN_CALL __cdecl
#if defined(NOVEN_PLUGIN_IMPLEMENTATION)
#define NOVEN_EXPORT __declspec(dllexport)
#else
#define NOVEN_EXPORT
#endif
#else
#define NOVEN_CALL
#define NOVEN_EXPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define NOVEN_NATIVE_ABI_VERSION 1u
#define NOVEN_PLUGIN_API_VERSION 1u
#define NOVEN_OK 0
#define NOVEN_ERROR_ARGUMENT -1
#define NOVEN_ERROR_PERMISSION -2
#define NOVEN_ERROR_LIMIT -3
#define NOVEN_ERROR_STATE -4
#define NOVEN_ERROR_TRANSPORT -5

/* UTF-8 切片不需要 NUL；调用期间有效，接收方必须复制，禁止跨 ABI 抛异常。
 * UTF-8 slices need no NUL; valid only during calls, receivers copy, no exceptions across ABI. */
typedef struct NovenUtf8V1 { const char* data; uint32_t length; } NovenUtf8V1;

/* context 是 Host 私有的不可解引用令牌，不是 Noven Core/UI 指针。
 * Context is an opaque Host-private token, never a Noven Core/UI pointer.
 * API 只允许初始化/动作/关闭回调所在的 Host 线程同步调用；不得保留异步任务。
 * API calls are synchronous on the Host callback thread only; no retained asynchronous tasks.
 * Host 表在 shutdown 返回前有效。日志无需页面权限；其他两项需要 ui.page.register。
 * Host table lives through shutdown. Logging needs no page grant; page APIs require ui.page.register. */
typedef struct NovenHostApiV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* context;
    int32_t (NOVEN_CALL *log)(void*, NovenUtf8V1);
    int32_t (NOVEN_CALL *register_page)(void*, NovenUtf8V1 local_id, NovenUtf8V1 title);
    int32_t (NOVEN_CALL *publish_page)(void*, NovenUtf8V1 local_id, NovenUtf8V1 document_json);
} NovenHostApiV1;

/* 插件自行拥有 context；shutdown 恰好调用一次（异常终止除外），负责释放插件状态。
 * Plugin owns context; shutdown is called once except forced/crash termination and frees plugin state.
 * 所有回调必需；输入切片只在回调期间有效。不得销毁 Host 表或借用切片。
 * All callbacks are required; input slices last only through callbacks. Never free the Host table or borrowed slices. */
typedef struct NovenPluginInstanceV1 {
    uint32_t struct_size;
    uint32_t abi_version;
    void* context;
    void (NOVEN_CALL *shutdown)(void*);
    int32_t (NOVEN_CALL *on_ui_action)(void*, NovenUtf8V1 local_page_id, NovenUtf8V1 action_id);
} NovenPluginInstanceV1;

typedef uint32_t (NOVEN_CALL *NovenGetAbiVersionFn)(void);
typedef int32_t (NOVEN_CALL *NovenInitializeFn)(const NovenHostApiV1*, NovenPluginInstanceV1*);
/* 保持 Phase 3 的两个基础表尺寸不变；目录 API 使用可选独立扩展导出。
 * Preserve both Phase 3 base-table sizes; catalog API uses an optional separate extension export.
 * manifest/API/native ABI/transport 仍各自为原版本，目录 schema 独立为 1。
 * Manifest/API/native ABI/transport retain their versions; catalog schema is independently 1. */
#define NOVEN_CATALOG_SCHEMA_VERSION 1u
#define NOVEN_CATALOG_ITEMS 1u
#define NOVEN_CATALOG_TASKS 2u
#define NOVEN_CATALOG_MAPS 3u
#define NOVEN_DATA_RAID_HISTORY 4u
#define NOVEN_CATALOG_EVENTS 5u
#define NOVEN_DATA_RECENT_SCANS 6u
#define NOVEN_DATA_LIST 1u
#define NOVEN_DATA_GET 2u
#define NOVEN_DATA_OK 0u
#define NOVEN_DATA_PERMISSION_DENIED 1u
#define NOVEN_DATA_NOT_FOUND 2u
#define NOVEN_DATA_INVALID_REQUEST 3u
#define NOVEN_DATA_UNAVAILABLE 4u
#define NOVEN_DATA_TOO_LARGE 5u
#define NOVEN_DATA_LIMITED 6u
typedef struct NovenDataRequestV1 {
    uint32_t struct_size;
    uint32_t catalog_kind;
    uint64_t request_id;
    uint32_t operation;
    uint32_t offset;
    uint32_t limit;
    NovenUtf8V1 stable_id_utf8;
} NovenDataRequestV1;
typedef struct NovenDataResultV1 {
    uint32_t struct_size;
    uint32_t status;
    uint64_t request_id;
    NovenUtf8V1 payload_utf8;
} NovenDataResultV1;
/* 仅回调线程可入队，立即返回，不在插件回调里做跨进程等待；Host 复制请求。
 * Callback-thread enqueue only, returning immediately without IPC waits; Host copies the request.
 * 成功入队后异步返回；关闭/崩溃可取消。结果切片仅在回调期间有效，插件要保留需复制。
 * Accepted requests complete asynchronously or cancel on stop/crash. Result slices are borrowed for the callback only.
 * 所有结果回调串行，context 为基础 instance.context；不引入第二份生命周期所有权。
 * Results are serialized callbacks using base instance.context; no second lifecycle ownership. */
typedef struct NovenCatalogHostApiV1 {
    uint32_t struct_size;
    uint32_t schema_version;
    void* context;
    int32_t (NOVEN_CALL *request_data)(void*, const NovenDataRequestV1*);
} NovenCatalogHostApiV1;
typedef struct NovenCatalogInstanceV1 {
    uint32_t struct_size;
    uint32_t schema_version;
    void (NOVEN_CALL *on_data_result)(void*, const NovenDataResultV1*);
} NovenCatalogInstanceV1;
typedef int32_t (NOVEN_CALL *NovenInitializeCatalogFn)(const NovenCatalogHostApiV1*, NovenCatalogInstanceV1*);
/* 独立可选扩展，不改变基础/目录 ABI 表。只提供完成通知，没有触发/捕获能力。
 * Independent optional extension preserving base/catalog tables. Completed notifications only, no trigger/capture.
 * 订阅调用仅入队；on_subscription_result 确认生效，之后开始通知，不回放历史。
 * Subscription calls only enqueue; on_subscription_result confirms activation, with no historical replay.
 * 回调串行使用基础 context，UTF-8 仅在回调期间有效；停止可能取消已排队事件。
 * Serialized callbacks use base context; UTF-8 is borrowed for the callback; stop may cancel queued events. */
#define NOVEN_SCAN_SCHEMA_VERSION 1u
typedef struct NovenScanEventV1 {
    uint32_t struct_size;
    uint32_t schema_version;
    uint64_t sequence;
    uint32_t dropped_events;
    NovenUtf8V1 record_utf8;
} NovenScanEventV1;
typedef struct NovenScanHostApiV1 {
    uint32_t struct_size;
    uint32_t schema_version;
    void* context;
    int32_t (NOVEN_CALL *subscribe)(void*);
    int32_t (NOVEN_CALL *unsubscribe)(void*);
} NovenScanHostApiV1;
typedef struct NovenScanInstanceV1 {
    uint32_t struct_size;
    uint32_t schema_version;
    void (NOVEN_CALL *on_subscription_result)(void*, uint32_t subscribed, int32_t status);
    void (NOVEN_CALL *on_scan_event)(void*, const NovenScanEventV1*);
} NovenScanInstanceV1;
typedef int32_t (NOVEN_CALL *NovenInitializeScanFn)(const NovenScanHostApiV1*, NovenScanInstanceV1*);
/* 独立可选存储扩展；不改变既有表。键不是路径，值为不透明字节，非凭据保险箱。
 * Optional independent storage extension preserving all old tables. Logical keys, opaque bytes, not a secret vault.
 * request 只在回调线程复制并入队；结果切片/键数组仅在回调期间有效。使用基础 instance.context。
 * Requests copy/enqueue on the callback thread; result slices/key arrays are borrowed for the callback, using base context. */
#define NOVEN_STORAGE_SCHEMA_VERSION 1u
#define NOVEN_STORAGE_GET 1u
#define NOVEN_STORAGE_SET 2u
#define NOVEN_STORAGE_DELETE 3u
#define NOVEN_STORAGE_LIST 4u
#define NOVEN_STORAGE_OK 0u
#define NOVEN_STORAGE_PERMISSION_DENIED 1u
#define NOVEN_STORAGE_NOT_FOUND 2u
#define NOVEN_STORAGE_INVALID_REQUEST 3u
#define NOVEN_STORAGE_QUOTA 4u
#define NOVEN_STORAGE_CORRUPT 5u
#define NOVEN_STORAGE_IO_ERROR 6u
#define NOVEN_STORAGE_UNAVAILABLE 7u
typedef struct NovenBytesV1 {const uint8_t* data;uint32_t length;} NovenBytesV1;
typedef struct NovenStorageRequestV1 {
    uint32_t struct_size,operation;
    uint64_t request_id;
    NovenUtf8V1 key_utf8;
    NovenBytesV1 value;
    uint32_t offset,limit;
} NovenStorageRequestV1;
typedef struct NovenStorageResultV1 {
    uint32_t struct_size,status;
    uint64_t request_id;
    NovenBytesV1 value;
    const NovenUtf8V1* keys;
    uint32_t key_count,total,next_offset;
} NovenStorageResultV1;
typedef struct NovenStorageHostApiV1 {
    uint32_t struct_size,schema_version;
    void* context;
    int32_t (NOVEN_CALL *storage_request)(void*,const NovenStorageRequestV1*);
} NovenStorageHostApiV1;
typedef struct NovenStorageInstanceV1 {
    uint32_t struct_size,schema_version;
    void (NOVEN_CALL *on_storage_result)(void*,const NovenStorageResultV1*);
} NovenStorageInstanceV1;
typedef int32_t (NOVEN_CALL *NovenInitializeStorageFn)(const NovenStorageHostApiV1*,NovenStorageInstanceV1*);
#if defined(NOVEN_PLUGIN_STORAGE_IMPLEMENTATION)
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeStorageV1(const NovenStorageHostApiV1*,NovenStorageInstanceV1*);
#endif
#if defined(NOVEN_PLUGIN_SCAN_IMPLEMENTATION)
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeScanV1(const NovenScanHostApiV1*, NovenScanInstanceV1*);
#endif
#if defined(NOVEN_PLUGIN_CATALOG_IMPLEMENTATION)
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeCatalogV1(const NovenCatalogHostApiV1*, NovenCatalogInstanceV1*);
#endif
/* 独立可选 HTTP 扩展，不改变任何既有表；只约束托管 API，不是原生网络沙箱。
 * Optional independent HTTP extension preserves all old tables; managed API only, not a native network sandbox.
 * 请求只在 Host 回调线程复制入队；结果回调串行，数组/字节/UTF-8 仅在该回调期间借用。
 * Requests copy/enqueue on the Host callback thread; serialized result arrays/bytes/UTF-8 are borrowed for that callback only. */
#define NOVEN_HTTP_SCHEMA_VERSION 1u
#define NOVEN_HTTP_GET 1u
#define NOVEN_HTTP_HEAD 2u
#define NOVEN_HTTP_POST 3u
#define NOVEN_HTTP_OK 0u
#define NOVEN_HTTP_PERMISSION_DENIED 1u
#define NOVEN_HTTP_INVALID_REQUEST 2u
#define NOVEN_HTTP_BLOCKED_DESTINATION 3u
#define NOVEN_HTTP_TOO_LARGE 4u
#define NOVEN_HTTP_TIMEOUT 5u
#define NOVEN_HTTP_CANCELLED 6u
#define NOVEN_HTTP_TRANSPORT_ERROR 7u
#define NOVEN_HTTP_REDIRECT_DENIED 8u
typedef struct NovenHttpHeaderV1 {NovenUtf8V1 name_utf8,value_utf8;} NovenHttpHeaderV1;
typedef struct NovenHttpRequestV1 {
    uint32_t struct_size,method;
    uint64_t request_id;
    NovenUtf8V1 url_utf8;
    const NovenHttpHeaderV1* headers;
    uint32_t header_count;
    NovenBytesV1 body;
} NovenHttpRequestV1;
typedef struct NovenHttpResultV1 {
    uint32_t struct_size,status;
    uint64_t request_id;
    uint32_t http_status;
    NovenUtf8V1 final_url_utf8;
    const NovenHttpHeaderV1* headers;
    uint32_t header_count;
    NovenBytesV1 body;
} NovenHttpResultV1;
typedef struct NovenHttpHostApiV1 {
    uint32_t struct_size,schema_version;
    void* context;
    int32_t (NOVEN_CALL *http_request)(void*,const NovenHttpRequestV1*);
} NovenHttpHostApiV1;
typedef struct NovenHttpInstanceV1 {
    uint32_t struct_size,schema_version;
    void (NOVEN_CALL *on_http_result)(void*,const NovenHttpResultV1*);
} NovenHttpInstanceV1;
typedef int32_t (NOVEN_CALL *NovenInitializeHttpFn)(const NovenHttpHostApiV1*,NovenHttpInstanceV1*);
#if defined(NOVEN_PLUGIN_HTTP_IMPLEMENTATION)
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeHttpV1(const NovenHttpHostApiV1*,NovenHttpInstanceV1*);
#endif
/* Host 初始化 instance.struct_size/abi_version；插件验证并填写其余成员，成功返回 NOVEN_OK。
 * Host initializes instance.struct_size/abi_version; plugin validates/fills remaining members, returns NOVEN_OK. */
#ifndef NOVEN_PLUGIN_OMIT_EXPORT_DECLARATIONS
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void);
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1*, NovenPluginInstanceV1*);
#endif
#ifdef __cplusplus
}
#endif
#endif
