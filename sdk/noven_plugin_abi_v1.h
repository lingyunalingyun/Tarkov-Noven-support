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
/* Host 初始化 instance.struct_size/abi_version；插件验证并填写其余成员，成功返回 NOVEN_OK。
 * Host initializes instance.struct_size/abi_version; plugin validates/fills remaining members, returns NOVEN_OK. */
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void);
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1*, NovenPluginInstanceV1*);
#ifdef __cplusplus
}
#endif
#endif
