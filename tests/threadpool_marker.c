/* Harmless DLL whose deferred unload is checked by the work-object smoke. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

static HANDLE marker_unload_event;

/* The tests keep this event open until detach is observed. A detach signal
 * proves the deferred FreeLibrary reached the DLL loader; work-callback wait
 * alone is not an unload-completion fence on native Windows. */
__declspec(dllexport) void marker_set_unload_event(HANDLE event)
{
    marker_unload_event = event;
}

__declspec(dllexport) int WINAPI marker_identity(void)
{
    /* Keep a real, original Win98 KERNEL32 import so the DLL has a normal
     * import/relocation path even though the test does not call this export. */
    return (int)(GetTickCount() & 0x7fffUL);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    if (reason == DLL_PROCESS_DETACH && !reserved && marker_unload_event)
        SetEvent(marker_unload_event);
    return TRUE;
}
