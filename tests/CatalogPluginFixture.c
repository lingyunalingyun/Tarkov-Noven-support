#define NOVEN_PLUGIN_IMPLEMENTATION
#define NOVEN_PLUGIN_CATALOG_IMPLEMENTATION
#include "noven_plugin_abi_v1.h"
#include <string.h>
#include <stddef.h>

/* 冻结 Phase 3 布局，扩展不得迫使原二进制重编译。
 * Freeze Phase 3 layout so extensions never force existing binaries to rebuild. */
_Static_assert(sizeof(NovenHostApiV1)==40,"Phase 3 x64 host table");
_Static_assert(sizeof(NovenPluginInstanceV1)==32,"Phase 3 x64 instance table");
_Static_assert(offsetof(NovenHostApiV1,publish_page)==32,"no base field reordering");
static const NovenHostApiV1* host_api;
static const NovenCatalogHostApiV1* catalog_api;
static unsigned completed;
static NovenUtf8V1 Text(const char* text){NovenUtf8V1 value={text,(uint32_t)strlen(text)};return value;}
static void NOVEN_CALL Shutdown(void* context){(void)context;catalog_api=0;}
static int32_t NOVEN_CALL Action(void* context,NovenUtf8V1 page,NovenUtf8V1 action){(void)context;(void)page;(void)action;return NOVEN_OK;}
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void){return NOVEN_NATIVE_ABI_VERSION;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance){
    if(host->struct_size!=40||instance->struct_size!=32)return NOVEN_ERROR_ARGUMENT;
    host_api=host;instance->context=&completed;instance->shutdown=Shutdown;instance->on_ui_action=Action;return NOVEN_OK;
}
static void NOVEN_CALL Result(void* context,const NovenDataResultV1* result){
    NovenDataRequestV1 request={0};
    if(context!=&completed||result->struct_size!=sizeof(*result)||result->status!=NOVEN_DATA_OK||!result->payload_utf8.length)return;
    ++completed;
    host_api->log(host_api->context,Text("Catalog callback"));
    if(result->request_id<=3){
        request.struct_size=sizeof(request);request.catalog_kind=(uint32_t)result->request_id;
        request.request_id=result->request_id+100;request.operation=NOVEN_DATA_GET;
        request.stable_id_utf8=Text(request.catalog_kind==NOVEN_CATALOG_ITEMS?"item_1":request.catalog_kind==NOVEN_CATALOG_TASKS?"task_1":"interchange");
        catalog_api->request_data(catalog_api->context,&request);
    }
}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeCatalogV1(const NovenCatalogHostApiV1* host,NovenCatalogInstanceV1* instance){
    uint32_t kind;NovenDataRequestV1 request={0};
    if(host->struct_size<sizeof(*host)||host->schema_version!=1||instance->struct_size<sizeof(*instance))return NOVEN_ERROR_ARGUMENT;
    catalog_api=host;instance->on_data_result=Result;
    request.struct_size=sizeof(request);request.operation=NOVEN_DATA_LIST;request.limit=32;
    for(kind=1;kind<=3;++kind){
        request.catalog_kind=kind;request.request_id=kind;
        if(host->request_data(host->context,&request)!=NOVEN_OK)return NOVEN_ERROR_STATE;
        if(host->request_data(host->context,&request)!=NOVEN_ERROR_STATE)return NOVEN_ERROR_STATE;
    }
    request.request_id=4;request.limit=65;
    if(host->request_data(host->context,&request)!=NOVEN_ERROR_ARGUMENT)return NOVEN_ERROR_STATE;
    return NOVEN_OK;
}
