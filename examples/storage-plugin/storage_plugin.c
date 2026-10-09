#define NOVEN_PLUGIN_IMPLEMENTATION
#define NOVEN_PLUGIN_STORAGE_IMPLEMENTATION
#include "noven_plugin_abi_v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct State {
    const NovenHostApiV1* host;
    const NovenStorageHostApiV1* storage;
    uint64_t next,pending;
    uint32_t counter,operation,keys;
    int32_t status;
} State;
static State* active;
static NovenUtf8V1 Text(const char* text){NovenUtf8V1 result={text,(uint32_t)strlen(text)};return result;}
static int Equal(NovenUtf8V1 text,const char* value){return text.length==strlen(value)&&memcmp(text.data,value,text.length)==0;}
static int32_t Publish(State* state){
    char document[1800];snprintf(document,sizeof(document),"{\"schemaVersion\":1,\"blocks\":[{\"type\":\"heading\",\"text\":\"Storage Demo\"},{\"type\":\"text\",\"text\":\"Stored counter: %u | Keys: %u | Status: %d\"},{\"type\":\"text\",\"text\":\"Logical key: counter. Persistence survives Disable and Noven restart. Not a secret vault.\"},{\"type\":\"button\",\"id\":\"save\",\"label\":\"Increment & Save\"},{\"type\":\"button\",\"id\":\"reload\",\"label\":\"Reload\"},{\"type\":\"button\",\"id\":\"list\",\"label\":\"List Keys\"},{\"type\":\"button\",\"id\":\"delete\",\"label\":\"Delete\"}]}",state->counter,state->keys,(int)state->status);
    return state->host->publish_page(state->host->context,Text("dashboard"),Text(document));
}
static int32_t Request(State* state,uint32_t operation){
    NovenStorageRequestV1 request={0};uint8_t bytes[4];int32_t status;unsigned i;
    if(!state->storage||state->pending||state->next>=INT64_MAX)return NOVEN_ERROR_STATE;
    request.struct_size=sizeof(request);request.request_id=++state->next;request.operation=operation;
    if(operation==NOVEN_STORAGE_LIST)request.limit=32;else request.key_utf8=Text("counter");
    if(operation==NOVEN_STORAGE_SET){for(i=0;i<4;++i)bytes[i]=(uint8_t)((state->counter+1)>>(8*i));request.value.data=bytes;request.value.length=4;}
    status=state->storage->storage_request(state->storage->context,&request);
    if(status==NOVEN_OK){state->pending=request.request_id;state->operation=operation;}state->status=status;return status;
}
static void NOVEN_CALL Shutdown(void* context){active=0;free(context);}
static int32_t NOVEN_CALL Action(void* context,NovenUtf8V1 page,NovenUtf8V1 action){
    State* state=(State*)context;uint32_t operation;
    if(!Equal(page,"dashboard"))return NOVEN_ERROR_ARGUMENT;
    if(Equal(action,"save"))operation=NOVEN_STORAGE_SET;else if(Equal(action,"reload"))operation=NOVEN_STORAGE_GET;
    else if(Equal(action,"list"))operation=NOVEN_STORAGE_LIST;else if(Equal(action,"delete"))operation=NOVEN_STORAGE_DELETE;else return NOVEN_ERROR_ARGUMENT;
    Request(state,operation);return Publish(state);
}
static void NOVEN_CALL Result(void* context,const NovenStorageResultV1* result){
    State* state=(State*)context;unsigned i;if(result->struct_size<sizeof(*result)||result->request_id!=state->pending)return;
    state->pending=0;state->status=(int32_t)result->status;
    if(result->status==NOVEN_STORAGE_OK){
        if(state->operation==NOVEN_STORAGE_SET)++state->counter;
        if(state->operation==NOVEN_STORAGE_DELETE)state->counter=0;
        if(state->operation==NOVEN_STORAGE_LIST)state->keys=result->total;
        if(state->operation==NOVEN_STORAGE_GET&&result->value.length==4){state->counter=0;for(i=0;i<4;++i)state->counter|=(uint32_t)result->value.data[i]<<(8*i);}
    }else if(result->status==NOVEN_STORAGE_NOT_FOUND)state->counter=0;
    Publish(state);
}
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void){return NOVEN_NATIVE_ABI_VERSION;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance){
    State* state;if(!host||!instance||host->struct_size<sizeof(*host)||host->api_version!=1||instance->struct_size<sizeof(*instance)||instance->abi_version!=1)return NOVEN_ERROR_ARGUMENT;
    state=(State*)calloc(1,sizeof(*state));if(!state)return NOVEN_ERROR_STATE;state->host=host;
    if(host->register_page(host->context,Text("dashboard"),Text("Storage Demo"))!=NOVEN_OK||Publish(state)!=NOVEN_OK){free(state);return NOVEN_ERROR_STATE;}
    active=state;instance->context=state;instance->shutdown=Shutdown;instance->on_ui_action=Action;return NOVEN_OK;
}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeStorageV1(const NovenStorageHostApiV1* host,NovenStorageInstanceV1* instance){
    if(!active||!host||!instance||host->struct_size<sizeof(*host)||host->schema_version!=1||instance->struct_size<sizeof(*instance))return NOVEN_ERROR_ARGUMENT;
    active->storage=host;instance->on_storage_result=Result;
    /* 初始化仅入队读取；未授权时保持页面可用，清楚显示拒绝。 Enqueue load only; keep page available with explicit denial when ungranted. */
    Request(active,NOVEN_STORAGE_GET);Publish(active);return NOVEN_OK;
}
