#define NOVEN_PLUGIN_IMPLEMENTATION
#define NOVEN_PLUGIN_OMIT_EXPORT_DECLARATIONS
#include "noven_plugin_abi_v1.h"
#include <windows.h>
#include <cstring>
#ifndef NOVEN_FIXTURE_MODE
#define NOVEN_FIXTURE_MODE 0
#endif
namespace {
const NovenHostApiV1* api{};
NovenUtf8V1 Text(const char* value){return {value,static_cast<uint32_t>(std::strlen(value))};}
void NOVEN_CALL Shutdown(void*){if(NOVEN_FIXTURE_MODE==7)Sleep(INFINITE);}
int32_t NOVEN_CALL Action(void*,NovenUtf8V1,NovenUtf8V1) {
    if(NOVEN_FIXTURE_MODE==9)Sleep(INFINITE);
    if(NOVEN_FIXTURE_MODE==10)TerminateProcess(GetCurrentProcess(),17);
    return api->publish_page(api->context,Text("dashboard"),Text(R"({"schemaVersion":1,"blocks":[{"type":"text","text":"Clicked"},{"type":"button","id":"refresh","label":"Refresh"}]})"));
}
}
#if NOVEN_FIXTURE_MODE != 1
extern "C" NOVEN_EXPORT uint32_t NOVEN_CALL NovenPlugin_GetAbiVersion(){return NOVEN_FIXTURE_MODE==2?2:NOVEN_NATIVE_ABI_VERSION;}
#endif
#if NOVEN_FIXTURE_MODE != 3
extern "C" NOVEN_EXPORT int32_t NOVEN_CALL NovenPlugin_Initialize(const NovenHostApiV1* host,NovenPluginInstanceV1* instance) {
    if(NOVEN_FIXTURE_MODE==5)return NOVEN_ERROR_STATE;
    if(NOVEN_FIXTURE_MODE==6)TerminateProcess(GetCurrentProcess(),16);
    if(NOVEN_FIXTURE_MODE==8)Sleep(INFINITE);
    if(host->struct_size!=sizeof(*host)||host->api_version!=1||instance->struct_size!=sizeof(*instance))return NOVEN_ERROR_ARGUMENT;
    api=host;instance->shutdown=&Shutdown;instance->on_ui_action=&Action;
    if(NOVEN_FIXTURE_MODE==4){instance->struct_size=1;return NOVEN_OK;}
    if(NOVEN_FIXTURE_MODE==12){instance->on_ui_action=nullptr;return NOVEN_OK;}
    if(NOVEN_FIXTURE_MODE==13) {
        return host->register_page(host->context,Text("dashboard"),Text("Denied"))==NOVEN_ERROR_PERMISSION
            &&host->publish_page(host->context,Text("dashboard"),Text("{}"))==NOVEN_ERROR_PERMISSION?NOVEN_OK:NOVEN_ERROR_STATE;
    }
    host->log(host->context,Text("Fixture initialized"));
    if(host->register_page(host->context,Text("builtin.plugins"),Text("Spoof"))!=NOVEN_ERROR_ARGUMENT)return NOVEN_ERROR_STATE;
    if(host->register_page(host->context,Text("dashboard"),Text("Fixture"))!=NOVEN_OK)return NOVEN_ERROR_STATE;
    if(host->register_page(host->context,Text("dashboard"),Text("Duplicate"))!=NOVEN_ERROR_STATE)return NOVEN_ERROR_STATE;
    if(NOVEN_FIXTURE_MODE==11&&host->publish_page(host->context,Text("dashboard"),Text(R"({"schemaVersion":1,"blocks":[{"type":"html","text":"x"}]})"))!=NOVEN_ERROR_ARGUMENT)return NOVEN_ERROR_STATE;
    if(NOVEN_FIXTURE_MODE==14){for(int i=0;i<32;++i)host->log(host->context,Text("Bounded log"));if(host->log(host->context,Text("Too many"))!=NOVEN_ERROR_LIMIT)return NOVEN_ERROR_STATE;}
    return host->publish_page(host->context,Text("dashboard"),Text(R"({"schemaVersion":1,"blocks":[{"type":"heading","text":"Fixture"},{"type":"button","id":"refresh","label":"Refresh"}]})"));
}
#endif
