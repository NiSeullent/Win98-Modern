/* Static ADVAPI32!RegGetValueA/W import probe for the guest KernelEx route. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

/* The declaration is gated out of MinGW headers for WINVER 0x0410. */
__declspec(dllimport) LONG WINAPI RegGetValueW(HKEY, const WCHAR *,
                                                const WCHAR *, DWORD,
                                                DWORD *, void *, DWORD *);
__declspec(dllimport) LONG WINAPI RegGetValueA(HKEY, const char *,
                                                const char *, DWORD,
                                                DWORD *, void *, DWORD *);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, NULL);
}

void mainCRTStartup(void)
{
    HKEY key;
    DWORD disposition, type = 0, bytes = sizeof(WCHAR) * 8;
    WCHAR result[8];
    char ansi_result[8];
    LONG status;
    if (RegCreateKeyExA(HKEY_CURRENT_USER,
                        "Software\\Win98Modern-RegGetValue-Import", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &key,
                        &disposition) != ERROR_SUCCESS) {
        say("FAIL: import fixture key\r\n");
        ExitProcess(1);
    }
    if (RegSetValueExA(key, "Value", 0, REG_SZ,
                       (const BYTE *)"probe", 6) != ERROR_SUCCESS) {
        say("FAIL: import fixture value\r\n");
        ExitProcess(2);
    }
    status = RegGetValueW(key, NULL, L"Value", 0x0000ffffUL,
                          &type, result, &bytes);
    if (status == ERROR_SUCCESS && type == REG_SZ &&
        bytes == sizeof(L"probe") && result[0] == L'p' && result[5] == 0) {
        bytes = sizeof(ansi_result);
        status = RegGetValueA(key, NULL, "Value", 0x0000ffffUL,
                              &type, ansi_result, &bytes);
    }
    RegCloseKey(key);
    RegDeleteKeyA(HKEY_CURRENT_USER,
                  "Software\\Win98Modern-RegGetValue-Import");
    if (status != ERROR_SUCCESS || type != REG_SZ ||
        bytes != sizeof("probe") || ansi_result[0] != 'p' ||
        ansi_result[5] != 0) {
        say("FAIL: static ADVAPI32 RegGetValueA/W behavior\r\n");
        ExitProcess(3);
    }
    say("PASS: static ADVAPI32 RegGetValueA/W imports and values\r\n");
    ExitProcess(0);
}
