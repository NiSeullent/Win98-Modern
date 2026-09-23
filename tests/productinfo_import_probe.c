/* Requires KernelEx routing of a static KERNEL32.GetProductInfo import. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllimport) BOOL WINAPI GetProductInfo(DWORD, DWORD, DWORD,
                                                  DWORD, DWORD *);

void mainCRTStartup(void)
{
    DWORD product = 0;
    const char *message;
    DWORD length = 0, written;
    BOOL ok = GetProductInfo(10, 0, 0, 0, &product);
    message = ok && product != PRODUCT_UNDEFINED ?
        "PASS: KERNEL32 static GetProductInfo behavior\r\n" :
        "FAIL: KERNEL32 static GetProductInfo behavior\r\n";
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, 0);
    ExitProcess(ok && product != PRODUCT_UNDEFINED ? 0 : 1);
}
