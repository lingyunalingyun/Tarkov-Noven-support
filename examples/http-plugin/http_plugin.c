#define NOVEN_PLUGIN_IMPLEMENTATION
#define NOVEN_PLUGIN_HTTP_IMPLEMENTATION
#include "noven_plugin_abi_v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct State {
    const NovenHostApiV1* host;const NovenHttpHostApiV1* http;
    uint64_t next,pending;int32_t status;uint32_t code;
    char preview[1100];
} State;
static State* active;
static NovenUtf8V1 Text(const char* text){NovenUtf8V1 value={text,(uint32_t)strlen(text)};return value;}
static int Equal(NovenUtf8V1 text,const char* value){return text.length==strlen(value)&&memcmp(text.data,value,text.length)==0;}
static int32_t Publish(State* state){
    char document[2200];snprintf(document,sizeof(document),"{\"schemaVersion\":1,\"blocks\":[{\"type\":\"heading\",\"text\":\"HTTP Demo\"},{\"type\":\"text\",\"text\":\"Status: %d | HTTP: %u | Pending: %u\"},{\"type\":\"text\",\"text\":\"%s\"},{\"type\":\"text\",\"text\":\"Granted origin: https://httpbin.org. Managed API only, not an OS network sandbox.\"},{\"type\":\"button\",\"id\":\"get\",\"label\":\"GET\"},{\"type\":\"button\",\"id\":\"head\",\"label\":\"HEAD\"},{\"type\":\"button\",\"id\":\"post\",\"label\":\"POST\"},{\"type\":\"button\",\"id\":\"denied\",\"label\":\"Try undeclared origin\"}]}",state->status,state->code,state->pending?1u:0u,state->preview);
    return state->host->publish_page(state->host->context,Text("dashboard"),Text(document));
}
static void NOVEN_CALL Shutdown(void* context){active=0;free(context);}
static int32_t NOVEN_CALL Action(void* context,NovenUtf8V1 page,NovenUtf8V1 action){
    State* state=(State*)context;NovenHttpRequestV1 request={0};const char* url="https://httpbin.org/get";
    if(!Equal(page,"dashboard")||!state->http||state->pending||state->next>=INT64_MAX)return NOVEN_ERROR_STATE;
    request.method=NOVEN_HTTP_GET;
    if(Equal(action,"head"))request.method=NOVEN_HTTP_HEAD;
    else if(Equal(action,"post")){request.method=NOVEN_HTTP_POST;url="https://httpbin.org/post";request.body.data=(const uint8_t*)"Noven HTTP demo";request.body.length=15;}
    else if(Equal(action,"denied"))url="https://example.org";
    else if(!Equal(action,"get"))return NOVEN_ERROR_ARGUMENT;
    request.struct_size=sizeof(request);request.request_id=++state->next;request.url_utf8=Text(url);
    state->status=state->http->http_request(state->http->context,&request);state->code=0;state->preview[0]=0;
    if(state->status==NOVEN_OK)state->pending=request.request_id;
    snprintf(state->preview,sizeof(state->preview),"%s",state->status==NOVEN_OK?"Waiting for explicit request result.":"Request rejected before network access.");
    return Publish(state);
}
static void NOVEN_CALL Result(void* context,const NovenHttpResultV1* result){
    State* state=(State*)context;uint32_t i;size_t used=0;
    if(result->struct_size<sizeof(*result)||result->request_id!=state->pending)return;
    state->pending=0;state->status=(int32_t)result->status;state->code=result->http_status;
    /* 只展示有界文本预览；非 ASCII 字节替换，JSON 元字符转义。 Bounded text preview, replace non-ASCII and escape JSON metacharacters. */
    for(i=0;i<result->body.length&&i<512;++i){unsigned char c=result->body.data[i];if(c=='"'||c=='\\')state->preview[used++]='\\';state->preview[used++]=(c>=32&&c<=126)?(char)c:' ';}
    state->preview[used]=0;if(!used)snprintf(state->preview,sizeof(state->preview),"No response body (HEAD or explicit error).");Publish(state);
}
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void){return NOVEN_NATIVE_ABI_VERSION;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance){
    State* state;if(!host||!instance||host->struct_size<sizeof(*host)||host->api_version!=1||instance->struct_size<sizeof(*instance)||instance->abi_version!=1)return NOVEN_ERROR_ARGUMENT;
    state=(State*)calloc(1,sizeof(*state));if(!state)return NOVEN_ERROR_STATE;state->host=host;snprintf(state->preview,sizeof(state->preview),"Ready. No requests until you click a button.");
    if(host->register_page(host->context,Text("dashboard"),Text("HTTP Demo"))!=NOVEN_OK||Publish(state)!=NOVEN_OK){free(state);return NOVEN_ERROR_STATE;}
    active=state;instance->context=state;instance->shutdown=Shutdown;instance->on_ui_action=Action;return NOVEN_OK;
}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeHttpV1(const NovenHttpHostApiV1* host,NovenHttpInstanceV1* instance){
    if(!active||!host||!instance||host->struct_size<sizeof(*host)||host->schema_version!=1||instance->struct_size<sizeof(*instance)||instance->schema_version!=1)return NOVEN_ERROR_ARGUMENT;
    active->http=host;instance->on_http_result=Result;return NOVEN_OK;
}
