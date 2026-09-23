/* Windows 98 memory baseline: report what the OS exposes, without stress. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

typedef struct m98_memory_status_ex {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    ULONGLONG ullTotalPhys;
    ULONGLONG ullAvailPhys;
    ULONGLONG ullTotalPageFile;
    ULONGLONG ullAvailPageFile;
    ULONGLONG ullTotalVirtual;
    ULONGLONG ullAvailVirtual;
    ULONGLONG ullAvailExtendedVirtual;
} m98_memory_status_ex;

typedef BOOL (WINAPI *memory_status_ex_fn)(m98_memory_status_ex *);

static void output(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, 0);
}

static void output_hex(const char *label, ULONGLONG value)
{
    static const char digits[] = "0123456789ABCDEF";
    char line[64];
    DWORD i = 0, nibble;
    while (label[i]) { line[i] = label[i]; i++; }
    line[i++] = '0'; line[i++] = 'x';
    for (nibble = 0; nibble < 16; nibble++)
        line[i++] = digits[(value >> ((15 - nibble) * 4)) & 0xf];
    line[i++] = '\r'; line[i++] = '\n'; line[i] = 0;
    output(line);
}

void mainCRTStartup(void)
{
    MEMORYSTATUS legacy;
    m98_memory_status_ex extended;
    HMODULE kernel32;
    memory_status_ex_fn get_extended;
    legacy.dwLength = sizeof(legacy);
    GlobalMemoryStatus(&legacy);
    output_hex("GlobalMemoryStatus.total_phys=", legacy.dwTotalPhys);
    output_hex("GlobalMemoryStatus.avail_phys=", legacy.dwAvailPhys);

    kernel32 = GetModuleHandleA("KERNEL32.DLL");
    get_extended = kernel32 ? (memory_status_ex_fn)GetProcAddress(
        kernel32, "GlobalMemoryStatusEx") : 0;
    if (get_extended) {
        extended.dwLength = sizeof(extended);
        if (get_extended(&extended)) {
            output_hex("GlobalMemoryStatusEx.total_phys=", extended.ullTotalPhys);
            output_hex("GlobalMemoryStatusEx.avail_phys=", extended.ullAvailPhys);
        } else {
            output("GlobalMemoryStatusEx.call_failed\r\n");
        }
    } else {
        output("GlobalMemoryStatusEx.unavailable\r\n");
    }
    ExitProcess(0);
}
