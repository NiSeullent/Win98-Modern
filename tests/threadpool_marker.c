/* Harmless DLL whose deferred unload is checked by the work-object smoke. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllexport) int WINAPI marker_identity(void)
{
    /* Keep a real, original Win98 KERNEL32 import so the DLL has a normal
     * import/relocation path even though the test does not call this export. */
    return (int)(GetTickCount() & 0x7fffUL);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
