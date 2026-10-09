#define NOVEN_PLUGIN_IMPLEMENTATION
#define NOVEN_PLUGIN_STORAGE_IMPLEMENTATION
#include "noven_plugin_abi_v1.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#ifndef NOVEN_STORAGE_FIXTURE_MODE
#define NOVEN_STORAGE_FIXTURE_MODE 0
#endif
static const NovenHostApiV1* base;
static const NovenStorageHostApiV1* storage;
static uint64_t next;
static unsigned batch_results;
static int batch;
static NovenUtf8V1 Text(const char* text){NovenUtf8V1 value={text,(uint32_t)strlen(text)};return value;}
static int Equal(NovenUtf8V1 value,const char* text){return value.length==strlen(text)&&memcmp(value.data,text,value.length)==0;}
static void NOVEN_CALL Shutdown(void* context){(void)context;}
static int32_t NOVEN_CALL Action(void* context,NovenUtf8V1 page,NovenUtf8V1 action){
    NovenStorageRequestV1 request={0};char log[128];int32_t status;unsigned i;(void)context;(void)page;
    request.struct_size=sizeof(request);request.request_id=++next;request.operation=NOVEN_STORAGE_GET;request.key_utf8=Text("shared-key");
    if(!Equal(action,"batch"))batch=0;
    if(Equal(action,"set")){request.operation=NOVEN_STORAGE_SET;request.value.data=(const uint8_t*)"fixture";request.value.length=7;}
    if(Equal(action,"list")){request.operation=NOVEN_STORAGE_LIST;request.key_utf8=Text("");request.limit=32;}
    if(Equal(action,"delete"))request.operation=NOVEN_STORAGE_DELETE;
    if(Equal(action,"bad"))request.key_utf8=Text("../other");
    if(Equal(action,"batch")){static uint8_t bytes[32768];batch=1;batch_results=0;request.operation=NOVEN_STORAGE_SET;request.value.data=bytes;request.value.length=sizeof(bytes);}
    status=storage->storage_request(storage->context,&request);
    if(Equal(action,"duplicate"))status=storage->storage_request(storage->context,&request);
    if(Equal(action,"flood"))for(i=0;i<32;++i){request.request_id=++next;status=storage->storage_request(storage->context,&request);}
    if(Equal(action,"batch"))for(i=1;i<16;++i){request.request_id=++next;status=storage->storage_request(storage->context,&request);}
    snprintf(log,sizeof(log),"admission:%d",(int)status);base->log(base->context,Text(log));
    {char document[1800];snprintf(document,sizeof(document),"{\"schemaVersion\":1,\"blocks\":[{\"type\":\"text\",\"text\":\"%s\"},{\"type\":\"button\",\"id\":\"get\",\"label\":\"Get\"},{\"type\":\"button\",\"id\":\"set\",\"label\":\"Set\"},{\"type\":\"button\",\"id\":\"list\",\"label\":\"List\"},{\"type\":\"button\",\"id\":\"delete\",\"label\":\"Delete\"},{\"type\":\"button\",\"id\":\"duplicate\",\"label\":\"Duplicate\"},{\"type\":\"button\",\"id\":\"flood\",\"label\":\"Flood\"},{\"type\":\"button\",\"id\":\"bad\",\"label\":\"Invalid key\"}]}",log);return base->publish_page(base->context,Text("dashboard"),Text(document));}
}
static void NOVEN_CALL Result(void* context,const NovenStorageResultV1* result){
    (void)context;(void)result;
#if NOVEN_STORAGE_FIXTURE_MODE == 1
    base->log(base->context,Text("callback-entered"));Sleep(INFINITE);
#elif NOVEN_STORAGE_FIXTURE_MODE == 2
    ExitProcess(42);
#else
    char log[128];
    if(batch){if(++batch_results==16)base->log(base->context,Text("batch:16"));return;}
    snprintf(log,sizeof(log),"result:%u:%u:%u",result->status,result->value.length,result->total);base->log(base->context,Text(log));
#endif
}
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void){return NOVEN_NATIVE_ABI_VERSION;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance){
    base=host;instance->shutdown=Shutdown;instance->on_ui_action=Action;
    if(host->register_page(host->context,Text("dashboard"),Text("Storage Fixture"))!=NOVEN_OK)return NOVEN_ERROR_STATE;
    return host->publish_page(host->context,Text("dashboard"),Text("{\"schemaVersion\":1,\"blocks\":[{\"type\":\"button\",\"id\":\"get\",\"label\":\"Get\"},{\"type\":\"button\",\"id\":\"set\",\"label\":\"Set\"},{\"type\":\"button\",\"id\":\"list\",\"label\":\"List\"},{\"type\":\"button\",\"id\":\"delete\",\"label\":\"Delete\"},{\"type\":\"button\",\"id\":\"duplicate\",\"label\":\"Duplicate\"},{\"type\":\"button\",\"id\":\"flood\",\"label\":\"Flood\"},{\"type\":\"button\",\"id\":\"bad\",\"label\":\"Invalid key\"},{\"type\":\"button\",\"id\":\"batch\",\"label\":\"Large batch\"}]}"));
}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeStorageV1(const NovenStorageHostApiV1* host,NovenStorageInstanceV1* instance){
    storage=host;instance->on_storage_result=Result;
#if NOVEN_STORAGE_FIXTURE_MODE == 3
    instance->struct_size=0;
#elif NOVEN_STORAGE_FIXTURE_MODE == 4
    instance->schema_version=2;
#elif NOVEN_STORAGE_FIXTURE_MODE == 5
    instance->on_storage_result=0;
#endif
    return NOVEN_OK;
}
