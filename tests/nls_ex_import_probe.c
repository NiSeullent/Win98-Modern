/* Static KERNEL32 import shape for post-integration Windows 98 guest run. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(*m98_get_table_fn)(void);
typedef int (WINAPI *m98_compare_fn)(const WCHAR *, DWORD, const WCHAR *, int,
                                     const WCHAR *, int, void *, void *, LPARAM);
extern void *m98_compare_iat __asm__("__imp__CompareStringEx@36");

static void fail(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, 0);
    ExitProcess(1);
}

static void report_value(const char *label, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char output[64];
    DWORD length = 0, written, index;
    while (label[length]) { output[length] = label[length]; ++length; }
    for (index = 0; index < 8; ++index)
        output[length + index] = digits[(value >> (28 - index * 4)) & 15];
    output[length + 8] = '\r';
    output[length + 9] = '\n';
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), output, length + 10, &written, 0);
}

void mainCRTStartup(void)
{
    WCHAR output[8];
    HMODULE kexbases_before = GetModuleHandleA("KEXBASES.DLL");
#ifdef M98_FORCE_KEXBASES
    HMODULE kexbases_forced = LoadLibraryA("C:\\WINDOWS\\KernelEx\\KEXBASES.DLL");
    report_value("KEXBASES forced=", (DWORD)kexbases_forced);
    report_value("KEXBASES force error=", GetLastError());
#endif
    int compared = CompareStringEx(L"en-US", NORM_IGNORECASE, L"Abc", -1,
                                   L"abc", -1, 0, 0, 0);
    DWORD compare_error = GetLastError();
    if (compared != CSTR_EQUAL) {
        HMODULE kexbases_after = GetModuleHandleA("KEXBASES.DLL");
        int legacy = CompareStringW(0x0409, NORM_IGNORECASE,
                                    L"Abc", -1, L"abc", -1);
        DWORD legacy_error = GetLastError();
        HMODULE provider_before = GetModuleHandleA("M98WRP12.DLL");
        FARPROC dynamic = GetProcAddress(GetModuleHandleA("KERNEL32.DLL"),
                                         "CompareStringEx");
        HMODULE wrapper = LoadLibraryA("m98wrap.dll");
        int direct = -1;
        DWORD direct_addr = 0;
        DWORD direct_error = 0;
        if (wrapper) {
            m98_get_table_fn get_table = (m98_get_table_fn)
                GetProcAddress(wrapper, "get_api_table");
            if (get_table) {
                const m98_api_table *table = get_table();
                int index;
                for (index = 0; index < table->named_apis_count; ++index) {
                    const char *left = table->named_apis[index].name;
                    const char *right = "CompareStringEx";
                    while (*left && *left == *right) { ++left; ++right; }
                    if (!*left && !*right) {
                        m98_compare_fn fn = (m98_compare_fn)
                            table->named_apis[index].addr;
                        direct_addr = table->named_apis[index].addr;
                        direct = fn(L"en-US", NORM_IGNORECASE,
                                    L"Abc", -1, L"abc", -1, 0, 0, 0);
                        direct_error = GetLastError();
                        break;
                    }
                }
            }
        }
        report_value("CompareStringEx result=", (DWORD)compared);
        report_value("CompareStringEx error=", compare_error);
        report_value("KEXBASES before=", (DWORD)kexbases_before);
        report_value("KEXBASES after=", (DWORD)kexbases_after);
        report_value("CompareStringW result=", (DWORD)legacy);
        report_value("CompareStringW error=", legacy_error);
        report_value("Provider module before=", (DWORD)provider_before);
        report_value("Static IAT address=", (DWORD)m98_compare_iat);
        report_value("Dynamic KERNEL32 address=", (DWORD)dynamic);
        report_value("Direct table address=", direct_addr);
        report_value("Direct table result=", (DWORD)direct);
        report_value("Direct table error=", direct_error);
        fail("FAIL: static CompareStringEx\r\n");
    }
    if (LCMapStringEx(L"en-US", LCMAP_UPPERCASE, L"a", -1,
                      output, 8, 0, 0, 0) != 2 ||
        output[0] != L'A' || output[1])
        fail("FAIL: static LCMapStringEx\r\n");
    ExitProcess(0);
}
