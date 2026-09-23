/* Static import requires KernelEx to resolve the new KERNEL32 name at load. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

__declspec(dllimport) DWORD WINAPI GetFinalPathNameByHandleW(
    HANDLE, WCHAR *, DWORD, DWORD);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, 0);
}

void mainCRTStartup(void)
{
    char image_name[MAX_PATH];
    WCHAR output[512];
    HANDLE file;
    DWORD size, index;

    if (!GetModuleFileNameA(0, image_name, MAX_PATH)) {
        say("FAIL: locate process image\r\n");
        ExitProcess(1);
    }
    file = CreateFileA(image_name, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        say("FAIL: open process image\r\n");
        ExitProcess(1);
    }
    size = GetFinalPathNameByHandleW(file, output, 512, 0);
    CloseHandle(file);
    if (size < 7 || size >= 512 || output[0] != L'\\' ||
        output[1] != L'\\' || output[2] != L'?' ||
        output[3] != L'\\' || output[size] != 0) {
        say("FAIL: KERNEL32 static GetFinalPathNameByHandleW behavior\r\n");
        ExitProcess(1);
    }
    for (index = 0; index < size; ++index)
        if (!output[index]) {
            say("FAIL: truncated final path\r\n");
            ExitProcess(1);
        }
    say("PASS: KERNEL32 static GetFinalPathNameByHandleW behavior\r\n");
    ExitProcess(0);
}
