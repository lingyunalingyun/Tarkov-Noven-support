#include "noven_plugin_abi_v1.h"
#include <stddef.h>
/* 公共 ABI 必须由 C 编译器接受，不依赖 C++ 或 Noven 内部头。
 * The public ABI must compile as C without C++ or internal Noven headers. */
_Static_assert(sizeof(uint32_t)==4,"fixed width ABI");
_Static_assert(offsetof(NovenHostApiV1,struct_size)==0,"host size prefix");
_Static_assert(offsetof(NovenPluginInstanceV1,struct_size)==0,"instance size prefix");
int main(void) {
    NovenHostApiV1 host={0};
    NovenPluginInstanceV1 instance={0};
    host.struct_size=(uint32_t)sizeof(host);
    instance.struct_size=(uint32_t)sizeof(instance);
    return host.struct_size>0&&instance.struct_size>0&&NOVEN_NATIVE_ABI_VERSION==1u&&NOVEN_PLUGIN_API_VERSION==1u ? 0 : 1;
}
