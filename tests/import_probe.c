/* Statically imports two post-Win98 KERNEL32 APIs to test KernelEx routing. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

__declspec(dllimport) int WINAPI CompareStringOrdinal(
    const WCHAR *left, int left_len, const WCHAR *right, int right_len,
    BOOL ignore_case);
__declspec(dllimport) DWORD WINAPI GetActiveProcessorCount(WORD group);

void mainCRTStartup(void)
{
    static const char ok[] = "PASS: static KERNEL32 imports via KernelEx\r\n";
    static const char fail[] = "FAIL: static KERNEL32 API result\r\n";
    DWORD written;
    if (CompareStringOrdinal(L"ABC", -1, L"abc", -1, TRUE) == CSTR_EQUAL &&
        GetActiveProcessorCount(0) >= 1) {
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ok, sizeof(ok)-1, &written, 0);
        ExitProcess(0);
    }
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), fail, sizeof(fail)-1, &written, 0);
    ExitProcess(1);
}
