/* Host smoke test of the 32-bit DBGHELP bridge and native IMAGEHLP result. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

typedef PIMAGE_NT_HEADERS (WINAPI *image_nt_header_fn)(PVOID);
static BYTE invalid[512];

static void report(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, NULL);
}

static void fail(const char *message)
{
    report("FAIL: ");
    report(message);
    report("\r\n");
    ExitProcess(1);
}

void mainCRTStartup(void)
{
    HMODULE shim = LoadLibraryA("dbghelp.dll");
    HMODULE imagehlp;
    image_nt_header_fn bridge, native;
    PIMAGE_NT_HEADERS expected, actual;
    DWORD native_error, bridge_error;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)GetModuleHandleA(NULL);

    if (!shim) fail("load app-local dbghelp.dll");
    bridge = (image_nt_header_fn)GetProcAddress(shim, "ImageNtHeader");
    if (!bridge || GetProcAddress(shim, "ImageNtHeader@4"))
        fail("undecorated ImageNtHeader export");
    imagehlp = LoadLibraryA("IMAGEHLP.DLL");
    if (!imagehlp) fail("load native IMAGEHLP.DLL");
    native = (image_nt_header_fn)GetProcAddress(imagehlp, "ImageNtHeader");
    if (!native) fail("native IMAGEHLP.ImageNtHeader export");
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        fail("test image DOS header");
    expected = native(dos);
    actual = bridge(dos);
    if (!expected || actual != expected ||
        actual != (PIMAGE_NT_HEADERS)((BYTE *)dos + dos->e_lfanew) ||
        actual->Signature != IMAGE_NT_SIGNATURE)
        fail("valid PE header result");
    SetLastError(0x4a4a);
    expected = native(invalid);
    native_error = GetLastError();
    SetLastError(0x4a4a);
    actual = bridge(invalid);
    bridge_error = GetLastError();
    if (expected != NULL || actual != NULL || bridge_error != native_error)
        fail("invalid PE header result");
    report("PASS: dbghelp bridge matches native IMAGEHLP.ImageNtHeader\r\n");
    FreeLibrary(imagehlp);
    FreeLibrary(shim);
    ExitProcess(0);
}
