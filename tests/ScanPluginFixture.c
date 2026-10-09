#define NOVEN_PLUGIN_IMPLEMENTATION
#define NOVEN_PLUGIN_CATALOG_IMPLEMENTATION
#define NOVEN_PLUGIN_SCAN_IMPLEMENTATION
#include "noven_plugin_abi_v1.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#ifndef NOVEN_SCAN_FIXTURE_MODE
#define NOVEN_SCAN_FIXTURE_MODE 0
#endif
static const NovenHostApiV1* base;
static const NovenScanHostApiV1* scans;
static const NovenCatalogHostApiV1* catalog;
static unsigned events;
static NovenUtf8V1 Text(const char* text){NovenUtf8V1 value={text,(uint32_t)strlen(text)};return value;}
static int Equal(NovenUtf8V1 value,const char* text){return value.length==strlen(text)&&memcmp(value.data,text,value.length)==0;}
static void NOVEN_CALL Shutdown(void* context){(void)context;}
static int32_t NOVEN_CALL Action(void* context,NovenUtf8V1 page,NovenUtf8V1 action){
    int32_t status=NOVEN_ERROR_ARGUMENT;char log[128];(void)context;(void)page;
    if(Equal(action,"subscribe"))status=scans->subscribe(scans->context);
    else if(Equal(action,"unsubscribe"))status=scans->unsubscribe(scans->context);
    else if(Equal(action,"history")){
        NovenDataRequestV1 request={0};request.struct_size=sizeof(request);request.catalog_kind=NOVEN_DATA_RECENT_SCANS;
        request.operation=NOVEN_DATA_LIST;request.request_id=1;request.limit=1;status=catalog->request_data(catalog->context,&request);
    }
    snprintf(log,sizeof(log),"admission:%s:%d",Equal(action,"history")?"history":Equal(action,"subscribe")?"subscribe":"unsubscribe",(int)status);base->log(base->context,Text(log));return NOVEN_OK;
}
static void NOVEN_CALL Subscription(void* context,uint32_t active,int32_t status){
    char log[128];(void)context;
#if NOVEN_SCAN_FIXTURE_MODE == 7
    Sleep(INFINITE);
#endif
    snprintf(log,sizeof(log),"subscription:%u:%d",active,(int)status);base->log(base->context,Text(log));
}
static void NOVEN_CALL Event(void* context,const NovenScanEventV1* event){
    (void)context;(void)event;
#if NOVEN_SCAN_FIXTURE_MODE == 1
    Sleep(INFINITE);
#elif NOVEN_SCAN_FIXTURE_MODE == 2
    ExitProcess(42);
#else
    char log[128];
    snprintf(log,sizeof(log),"event:%u:%llu:%u",++events,(unsigned long long)event->sequence,event->dropped_events);base->log(base->context,Text(log));
#endif
}
static void NOVEN_CALL Result(void* context,const NovenDataResultV1* result){
    char log[128];(void)context;snprintf(log,sizeof(log),"history:%u",result->status);base->log(base->context,Text(log));
}
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void){return NOVEN_NATIVE_ABI_VERSION;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance){
    static const char* document="{\"schemaVersion\":1,\"blocks\":[{\"type\":\"button\",\"id\":\"subscribe\",\"label\":\"Subscribe\"},{\"type\":\"button\",\"id\":\"unsubscribe\",\"label\":\"Unsubscribe\"},{\"type\":\"button\",\"id\":\"history\",\"label\":\"History\"}]}";
    if(host->struct_size!=sizeof(*host)||instance->struct_size!=sizeof(*instance))return NOVEN_ERROR_ARGUMENT;
    base=host;events=0;instance->shutdown=Shutdown;instance->on_ui_action=Action;
    if(host->register_page(host->context,Text("dashboard"),Text("Scan Fixture"))!=NOVEN_OK)return NOVEN_ERROR_STATE;
    return host->publish_page(host->context,Text("dashboard"),Text(document));
}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeCatalogV1(const NovenCatalogHostApiV1* host,NovenCatalogInstanceV1* instance){catalog=host;instance->on_data_result=Result;return NOVEN_OK;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeScanV1(const NovenScanHostApiV1* host,NovenScanInstanceV1* instance){
    if(host->struct_size<sizeof(*host)||instance->struct_size<sizeof(*instance))return NOVEN_ERROR_ARGUMENT;
    scans=host;instance->on_scan_event=Event;instance->on_subscription_result=Subscription;
#if NOVEN_SCAN_FIXTURE_MODE == 3
    instance->struct_size=1;
#elif NOVEN_SCAN_FIXTURE_MODE == 4
    instance->on_scan_event=0;
#elif NOVEN_SCAN_FIXTURE_MODE == 5
    instance->schema_version=2;
#elif NOVEN_SCAN_FIXTURE_MODE == 6
    return NOVEN_ERROR_STATE;
#else
    return NOVEN_OK;
#endif
#if NOVEN_SCAN_FIXTURE_MODE >= 3 && NOVEN_SCAN_FIXTURE_MODE <= 5
    return NOVEN_OK;
#endif
}
