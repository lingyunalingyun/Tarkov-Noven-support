#define NOVEN_PLUGIN_IMPLEMENTATION
#include "noven_plugin_abi_v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct HelloState {const NovenHostApiV1* host;uint32_t counter;} HelloState;
static NovenUtf8V1 Text(const char* text){NovenUtf8V1 value={text,(uint32_t)strlen(text)};return value;}
static int Equal(NovenUtf8V1 value,const char* expected){return value.length==strlen(expected)&&memcmp(value.data,expected,value.length)==0;}
static int32_t Publish(HelloState* state){
    char document[1024];
    int length=snprintf(document,sizeof(document),"{\"schemaVersion\":1,\"blocks\":[{\"type\":\"heading\",\"text\":\"Hello from PluginHost\"},{\"type\":\"text\",\"text\":\"First-party example. No network or game access.\"},{\"type\":\"keyValue\",\"key\":\"Clicks\",\"value\":\"%u\"},{\"type\":\"badge\",\"text\":\"Process isolation is not an OS sandbox\"},{\"type\":\"separator\"},{\"type\":\"button\",\"id\":\"increment\",\"label\":\"Increment counter\"}]}",state->counter);
    if(length<0||(size_t)length>=sizeof(document))return NOVEN_ERROR_LIMIT;
    return state->host->publish_page(state->host->context,Text("dashboard"),Text(document));
}
static void NOVEN_CALL Shutdown(void* context){free(context);}
static int32_t NOVEN_CALL Action(void* context,NovenUtf8V1 page,NovenUtf8V1 action){
    HelloState* state=(HelloState*)context;
    if(!Equal(page,"dashboard")||!Equal(action,"increment"))return NOVEN_ERROR_ARGUMENT;
    if(state->counter<UINT32_MAX)++state->counter;
    return Publish(state);
}
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void){return NOVEN_NATIVE_ABI_VERSION;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance){
    HelloState* state;
    if(!host||!instance||host->struct_size!=sizeof(*host)||host->api_version!=NOVEN_PLUGIN_API_VERSION
        ||instance->struct_size!=sizeof(*instance)||instance->abi_version!=NOVEN_NATIVE_ABI_VERSION
        ||!host->log||!host->register_page||!host->publish_page)return NOVEN_ERROR_ARGUMENT;
    state=(HelloState*)calloc(1,sizeof(*state));if(!state)return NOVEN_ERROR_STATE;state->host=host;
    /* 初始化失败由插件清理；成功后状态仅由 shutdown 释放。
     * Failed initialization cleans up here; successful ownership ends in shutdown. */
    if(host->log(host->context,Text("Hello example initialized"))!=NOVEN_OK
        ||host->register_page(host->context,Text("dashboard"),Text("Hello Plugin"))!=NOVEN_OK||Publish(state)!=NOVEN_OK){free(state);return NOVEN_ERROR_STATE;}
    instance->context=state;instance->shutdown=Shutdown;instance->on_ui_action=Action;return NOVEN_OK;
}
