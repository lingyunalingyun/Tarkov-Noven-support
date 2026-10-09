#define NOVEN_PLUGIN_IMPLEMENTATION
#define NOVEN_PLUGIN_HTTP_IMPLEMENTATION
#include "noven_plugin_abi_v1.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
static const NovenHostApiV1* ui;static const NovenHttpHostApiV1* http;
static uint64_t next;
static NovenUtf8V1 Text(const char* text){NovenUtf8V1 value={text,(uint32_t)strlen(text)};return value;}
static void NOVEN_CALL Shutdown(void* context){(void)context;}
static int32_t NOVEN_CALL Action(void* context,NovenUtf8V1 page,NovenUtf8V1 action){
    NovenHttpRequestV1 request={0};char document[200];int32_t status;(void)context;(void)page;(void)action;
    request.struct_size=sizeof(request);request.method=NOVEN_HTTP_GET;request.request_id=++next;
    request.url_utf8=Text(NOVEN_HTTP_FIXTURE_MODE==3?"https://api.example.com/pending":"https://api.example.com/get");
    status=http->http_request(http->context,&request);
    snprintf(document,sizeof(document),"{\"schemaVersion\":1,\"blocks\":[{\"type\":\"text\",\"text\":\"Admission: %d\"},{\"type\":\"button\",\"id\":\"get\",\"label\":\"GET\"}]}",status);
    return ui->publish_page(ui->context,Text("dashboard"),Text(document));
}
static void NOVEN_CALL Result(void* context,const NovenHttpResultV1* result){
    (void)context;(void)result;
#if NOVEN_HTTP_FIXTURE_MODE==1
    /* 有意崩溃，只供独立 Host 测试。 Deliberate isolated Host test crash. */
    RaiseException(0xe0421234,EXCEPTION_NONCONTINUABLE,0,0);
#elif NOVEN_HTTP_FIXTURE_MODE==2
    Sleep(INFINITE);
#endif
}
NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(void){return 1;}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance){
    ui=host;instance->context=0;instance->shutdown=Shutdown;instance->on_ui_action=Action;
    if(ui->register_page(ui->context,Text("dashboard"),Text("HTTP Fixture"))!=0)return -4;
    return ui->publish_page(ui->context,Text("dashboard"),Text("{\"schemaVersion\":1,\"blocks\":[{\"type\":\"button\",\"id\":\"get\",\"label\":\"GET\"}]}"));
}
NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_InitializeHttpV1(const NovenHttpHostApiV1* host,NovenHttpInstanceV1* instance){
    http=host;instance->on_http_result=Result;
#if NOVEN_HTTP_FIXTURE_MODE==4
    instance->struct_size=0;
#elif NOVEN_HTTP_FIXTURE_MODE==5
    instance->schema_version=2;
#elif NOVEN_HTTP_FIXTURE_MODE==6
    instance->on_http_result=0;
#endif
    return 0;
}
