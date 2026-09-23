/* KernelEx COMCTL32 v6 ordinal provider. GPL-2.0-only. */
#include "m98_comctl_ordinals.h"
#include "kex_abi.h"

/* The KernelEx SDK uses an eight-byte ordinal entry on 32-bit x86. */
#define M98_NAMED(name, implementation) { name, (unsigned long)(implementation) }
#define M98_ORD(number, implementation) { number, (unsigned long)(implementation) }
static const m98_named_api named[] = {
    M98_NAMED("LoadIconWithScaleDown", m98_LoadIconWithScaleDown),
    M98_NAMED("TaskDialogIndirect", m98_TaskDialogIndirect)
};
static const m98_ordinal_api ordinal[] = {
    M98_ORD(345, m98_TaskDialogIndirect),
    M98_ORD(381, m98_LoadIconWithScaleDown)
};
static const m98_api_table tables[] = {
    { "COMCTL32.DLL", named, 2, ordinal, 2 },
    { 0, 0, 0, 0, 0 }
};

__declspec(dllexport) const m98_api_table *get_api_table(void)
{
    return tables;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
