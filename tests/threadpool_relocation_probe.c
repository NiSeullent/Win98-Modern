/* Force TPMARK.DLL away from its preferred base and call its native import. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

typedef int (WINAPI *marker_fn)(void);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, NULL);
}

void mainCRTStartup(void)
{
    void *reservation;
    HMODULE fixture, marker;
    marker_fn identity;
    reservation = VirtualAlloc((void *)(ULONG_PTR)0x64000000UL, 0x10000UL,
                               MEM_RESERVE, PAGE_NOACCESS);
    if (reservation != (void *)(ULONG_PTR)0x64000000UL) {
        say("FAIL: reserve marker preferred image base\r\n");
        ExitProcess(1);
    }
    fixture = LoadLibraryA("threadpool_fixture.dll");
    marker = LoadLibraryA("TPMARK.DLL");
    if (!fixture || !marker || marker == (HMODULE)reservation) {
        say("FAIL: relocate TPMARK.DLL while fixture is loaded\r\n");
        ExitProcess(1);
    }
    identity = (marker_fn)(ULONG_PTR)GetProcAddress(marker, "marker_identity@0");
    if (!identity || identity() < 0) {
        say("FAIL: relocated marker native import call\r\n");
        ExitProcess(1);
    }
    FreeLibrary(marker);
    FreeLibrary(fixture);
    VirtualFree(reservation, 0, MEM_RELEASE);
    say("PASS: marker DLL relocated and native import called\r\n");
    ExitProcess(0);
}
