#define NOVEN_PLUGIN_IMPLEMENTATION
#define NOVEN_PLUGIN_CATALOG_IMPLEMENTATION
#define NOVEN_PLUGIN_SCAN_IMPLEMENTATION
#include "noven_plugin_abi_v1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct ScanState {
    const NovenHostApiV1* host;
    const NovenCatalogHostApiV1* catalog;
    const NovenScanHostApiV1* scans;
    uint64_t next_id,pending_id;
    uint32_t subscribed,received,dropped;
    char history[1400],latest[1400];
} ScanState;
static ScanState* active;
static NovenUtf8V1 Text(const char* text){NovenUtf8V1 value={text,(uint32_t)strlen(text)};return value;}
static int Equal(NovenUtf8V1 text,const char* value){return text.length==strlen(value)&&memcmp(text.data,value,text.length)==0;}
static void Copy(char output[1400],NovenUtf8V1 input){
    size_t length=input.length;if(length>=1400)length=1399;
    if(length<input.length)while(length&&((unsigned char)input.data[length]&0xc0)==0x80)--length;
    memcpy(output,input.data,length);output[length]=0;
}
static int Escape(const char* text,char* output,size_t capacity){
    size_t at=0;const unsigned char* p=(const unsigned char*)text;
    for(;*p;++p){if(at+7>=capacity)return 0;
        if(*p=='"'||*p=='\\'){output[at++]='\\';output[at++]=(char)*p;}
        else if(*p<32){int count=snprintf(output+at,capacity-at,"\\u%04x",(unsigned)*p);if(count!=6)return 0;at+=6;}
        else output[at++]=(char)*p;
    }output[at]=0;return 1;
}
static int32_t Publish(ScanState* state){
    char history[8500],latest[8500],document[20000];int count;
    if(!Escape(state->history,history,sizeof(history))||!Escape(state->latest,latest,sizeof(latest)))return NOVEN_ERROR_LIMIT;
    count=snprintf(document,sizeof(document),"{\"schemaVersion\":1,\"blocks\":[{\"type\":\"heading\",\"text\":\"Scan Demo\"},{\"type\":\"text\",\"text\":\"Subscription: %s | Received: %u | Dropped: %u\"},{\"type\":\"text\",\"text\":\"Only completed Noven user scans. Use F2 yourself; plugins cannot trigger or capture.\"},{\"type\":\"button\",\"id\":\"history\",\"label\":\"Load history\"},{\"type\":\"button\",\"id\":\"subscribe\",\"label\":\"Subscribe\"},{\"type\":\"button\",\"id\":\"unsubscribe\",\"label\":\"Unsubscribe\"},{\"type\":\"heading\",\"text\":\"Recent Scans\"},{\"type\":\"text\",\"text\":\"%s\"},{\"type\":\"heading\",\"text\":\"Latest scan.completed\"},{\"type\":\"text\",\"text\":\"%s\"}]}",state->subscribed?"Active":"Inactive",state->received,state->dropped,history,latest);
    if(count<0||(size_t)count>=sizeof(document))return NOVEN_ERROR_LIMIT;
    return state->host->publish_page(state->host->context,Text("dashboard"),Text(document));
}
static void NOVEN_CALL Shutdown(void* context){active=0;free(context);}
static int32_t NOVEN_CALL Action(void* context,NovenUtf8V1 page,NovenUtf8V1 action){
    ScanState* state=(ScanState*)context;int32_t status=NOVEN_ERROR_STATE;
    if(!Equal(page,"dashboard"))return NOVEN_ERROR_ARGUMENT;
    if(Equal(action,"history")){
        if(state->catalog&&!state->pending_id&&state->next_id<INT64_MAX){
            NovenDataRequestV1 request={0};request.struct_size=sizeof(request);request.catalog_kind=NOVEN_DATA_RECENT_SCANS;
            request.operation=NOVEN_DATA_LIST;request.request_id=++state->next_id;request.limit=4;
            status=state->catalog->request_data(state->catalog->context,&request);if(status==NOVEN_OK)state->pending_id=request.request_id;
        }snprintf(state->history,sizeof(state->history),"History admission status: %d",(int)status);
    }else if(Equal(action,"subscribe")){if(state->scans)status=state->scans->subscribe(state->scans->context);}
    else if(Equal(action,"unsubscribe")){if(state->scans)status=state->scans->unsubscribe(state->scans->context);}
    else return NOVEN_ERROR_ARGUMENT;
    if(status!=NOVEN_OK)snprintf(state->latest,sizeof(state->latest),"Admission status: %d",(int)status);return Publish(state);
}
static void NOVEN_CALL Result(void* context,const NovenDataResultV1* result){
    ScanState* state=(ScanState*)context;if(result->struct_size<sizeof(*result)||result->request_id!=state->pending_id)return;
    state->pending_id=0;Copy(state->history,result->payload_utf8);Publish(state);
}
static void NOVEN_CALL Subscription(void* context,uint32_t subscribed,int32_t status){
    ScanState* state=(ScanState*)context;state->subscribed=subscribed;
    if(status!=NOVEN_OK)snprintf(state->latest,sizeof(state->latest),"Subscription denied: %d",(int)status);Publish(state);
}
static void NOVEN_CALL Event(void* context,const NovenScanEventV1* event){
    ScanState* state=(ScanState*)context;if(event->struct_size<sizeof(*event)||event->schema_version!=1)return;
    ++state->received;state->dropped=event->dropped_events;Copy(state->latest,event->record_utf8);Publish(state);
}
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void){return NOVEN_NATIVE_ABI_VERSION;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance){
    ScanState* state;if(!host||!instance||host->struct_size<sizeof(*host)||host->api_version!=1||instance->struct_size<sizeof(*instance)||instance->abi_version!=1)return NOVEN_ERROR_ARGUMENT;
    state=(ScanState*)calloc(1,sizeof(*state));if(!state)return NOVEN_ERROR_STATE;state->host=host;
    snprintf(state->history,sizeof(state->history),"Load history to read persisted safe records.");snprintf(state->latest,sizeof(state->latest),"No event before subscription. No historical replay.");
    if(host->register_page(host->context,Text("dashboard"),Text("Scan Demo"))!=NOVEN_OK||Publish(state)!=NOVEN_OK){free(state);return NOVEN_ERROR_STATE;}
    active=state;instance->context=state;instance->shutdown=Shutdown;instance->on_ui_action=Action;return NOVEN_OK;
}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeCatalogV1(const NovenCatalogHostApiV1* host,NovenCatalogInstanceV1* instance){
    if(!active||!host||!instance||host->struct_size<sizeof(*host)||host->schema_version!=1||instance->struct_size<sizeof(*instance))return NOVEN_ERROR_ARGUMENT;
    active->catalog=host;instance->on_data_result=Result;return NOVEN_OK;
}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeScanV1(const NovenScanHostApiV1* host,NovenScanInstanceV1* instance){
    if(!active||!host||!instance||host->struct_size<sizeof(*host)||host->schema_version!=1||instance->struct_size<sizeof(*instance))return NOVEN_ERROR_ARGUMENT;
    active->scans=host;instance->on_scan_event=Event;instance->on_subscription_result=Subscription;return NOVEN_OK;
}
