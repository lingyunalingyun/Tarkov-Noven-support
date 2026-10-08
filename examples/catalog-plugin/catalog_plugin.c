#define NOVEN_PLUGIN_IMPLEMENTATION
#define NOVEN_PLUGIN_CATALOG_IMPLEMENTATION
#include "noven_plugin_abi_v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Pending {uint64_t id;uint32_t kind,operation;} Pending;
typedef struct CatalogState {
    const NovenHostApiV1* host;
    const NovenCatalogHostApiV1* catalog;
    uint64_t next_id;
    Pending pending[16];
    char result[1400];
} CatalogState;
static CatalogState* active;
static NovenUtf8V1 Text(const char* text){NovenUtf8V1 value={text,(uint32_t)strlen(text)};return value;}
static int Equal(NovenUtf8V1 text,const char* value){return text.length==strlen(value)&&memcmp(text.data,value,text.length)==0;}
static int Quote(const char* text,char* output,size_t capacity){
    size_t at=0;const unsigned char* p=(const unsigned char*)text;
    for(;*p;++p){
        if(at+7>=capacity)return 0;
        if(*p=='"'||*p=='\\'){output[at++]='\\';output[at++]=(char)*p;}
        else if(*p<32){int count=snprintf(output+at,capacity-at,"\\u%04x",(unsigned)*p);if(count!=6)return 0;at+=6;}
        else output[at++]=(char)*p;
    }
    output[at]=0;return 1;
}
static int32_t Publish(CatalogState* state){
    char escaped[8500],document[10000];int count;
    if(!Quote(state->result,escaped,sizeof(escaped)))return NOVEN_ERROR_LIMIT;
    count=snprintf(document,sizeof(document),"{\"schemaVersion\":1,\"blocks\":[{\"type\":\"heading\",\"text\":\"Catalog Demo\"},{\"type\":\"text\",\"text\":\"Static read-only catalog copies. A list result triggers get-by-stable-ID.\"},{\"type\":\"button\",\"id\":\"items\",\"label\":\"Load Items\"},{\"type\":\"button\",\"id\":\"tasks\",\"label\":\"Load Tasks\"},{\"type\":\"button\",\"id\":\"maps\",\"label\":\"Load Maps\"},{\"type\":\"separator\"},{\"type\":\"text\",\"text\":\"%s\"}]}",escaped);
    if(count<0||(size_t)count>=sizeof(document))return NOVEN_ERROR_LIMIT;
    return state->host->publish_page(state->host->context,Text("dashboard"),Text(document));
}
static int32_t Request(CatalogState* state,uint32_t kind,uint32_t operation,const char* id){
    unsigned index;NovenDataRequestV1 request={0};int32_t status;
    if(!state->catalog)return NOVEN_ERROR_STATE;
    for(index=0;index<16&&state->pending[index].id;++index){}
    if(index==16||state->next_id>=INT64_MAX)return NOVEN_ERROR_LIMIT;
    request.struct_size=sizeof(request);request.catalog_kind=kind;request.operation=operation;request.request_id=++state->next_id;
    request.limit=operation==NOVEN_DATA_LIST?4:0;if(id)request.stable_id_utf8=Text(id);
    status=state->catalog->request_data(state->catalog->context,&request);
    if(status==NOVEN_OK){state->pending[index].id=request.request_id;state->pending[index].kind=kind;state->pending[index].operation=operation;}
    return status;
}
static void NOVEN_CALL Shutdown(void* context){active=0;free(context);}
static int32_t NOVEN_CALL Action(void* context,NovenUtf8V1 page,NovenUtf8V1 action){
    CatalogState* state=(CatalogState*)context;uint32_t kind;int32_t status;
    if(!Equal(page,"dashboard"))return NOVEN_ERROR_ARGUMENT;
    kind=Equal(action,"items")?NOVEN_CATALOG_ITEMS:Equal(action,"tasks")?NOVEN_CATALOG_TASKS:Equal(action,"maps")?NOVEN_CATALOG_MAPS:0;
    if(!kind)return NOVEN_ERROR_ARGUMENT;
    status=Request(state,kind,NOVEN_DATA_LIST,0);
    snprintf(state->result,sizeof(state->result),"Request %llu queued; admission status %d",(unsigned long long)state->next_id,(int)status);
    return Publish(state);
}
/* 仅从 Core 的规范 JSON 复制已验证的 ASCII stable ID；此辅助函数不是清单/通用 JSON 解析器。
 * Copy a validated ASCII stable ID from Core's canonical JSON only; this is not a manifest/general JSON parser. */
static int FirstId(const char* payload,uint32_t kind,char output[129]){
    const char* key=kind==NOVEN_CATALOG_ITEMS?"\"stableItemId\":\"":kind==NOVEN_CATALOG_TASKS?"\"stableTaskId\":\"":"\"stableMapId\":\"";
    const char* first=strstr(payload,key);size_t length=0;if(!first)return 0;first+=strlen(key);
    while(first[length]&&first[length]!='"'&&length<128){
        const unsigned char c=(unsigned char)first[length];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0;
        output[length]=(char)c;++length;
    }
    if(!length||first[length]!='"')return 0;output[length]=0;return 1;
}
static void NOVEN_CALL Result(void* context,const NovenDataResultV1* result){
    CatalogState* state=(CatalogState*)context;unsigned index;Pending request;char payload[24577],id[129];size_t copied;
    if(result->struct_size<sizeof(*result)||result->payload_utf8.length>24576)return;
    for(index=0;index<16&&state->pending[index].id!=result->request_id;++index){}
    if(index==16)return;request=state->pending[index];state->pending[index].id=0;
    memcpy(payload,result->payload_utf8.data,result->payload_utf8.length);payload[result->payload_utf8.length]=0;
    copied=result->payload_utf8.length;if(copied>sizeof(state->result)-1)copied=sizeof(state->result)-1;
    while(copied&&((unsigned char)payload[copied]&0xc0)==0x80)--copied;
    memcpy(state->result,payload,copied);state->result[copied]=0;
    Publish(state);
    if(result->status==NOVEN_DATA_OK&&request.operation==NOVEN_DATA_LIST&&FirstId(payload,request.kind,id))Request(state,request.kind,NOVEN_DATA_GET,id);
}
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void){return NOVEN_NATIVE_ABI_VERSION;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance){
    CatalogState* state;
    if(!host||!instance||host->struct_size!=sizeof(*host)||instance->struct_size!=sizeof(*instance)||host->api_version!=1||instance->abi_version!=1)return NOVEN_ERROR_ARGUMENT;
    state=(CatalogState*)calloc(1,sizeof(*state));if(!state)return NOVEN_ERROR_STATE;state->host=host;
    snprintf(state->result,sizeof(state->result),"Choose a catalog. No request runs until a button is clicked.");
    if(host->register_page(host->context,Text("dashboard"),Text("Catalog Demo"))!=NOVEN_OK||Publish(state)!=NOVEN_OK){free(state);return NOVEN_ERROR_STATE;}
    active=state;instance->context=state;instance->shutdown=Shutdown;instance->on_ui_action=Action;return NOVEN_OK;
}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeCatalogV1(const NovenCatalogHostApiV1* host,NovenCatalogInstanceV1* instance){
    if(!active||!host||!instance||host->struct_size<sizeof(*host)||host->schema_version!=1||instance->struct_size<sizeof(*instance))return NOVEN_ERROR_ARGUMENT;
    active->catalog=host;instance->on_data_result=Result;return NOVEN_OK;
}
