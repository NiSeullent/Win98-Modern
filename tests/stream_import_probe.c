/* Static KERNEL32 import probe: host native behavior and guest loader ABI. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

typedef struct stream_data {
    LARGE_INTEGER stream_size;
    WCHAR stream_name[MAX_PATH + 36];
} stream_data;
typedef char stream_data_is_600_bytes[(sizeof(stream_data) == 600) ? 1 : -1];

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, 0);
}

static void fail(const char *message)
{
    say("FAIL: ");
    say(message);
    say("\r\n");
    ExitProcess(1);
}

void mainCRTStartup(void)
{
    char image_a[MAX_PATH];
    WCHAR image_w[MAX_PATH];
    stream_data data;
    HANDLE search;
    DWORD error;

    if (!GetModuleFileNameA(GetModuleHandleA("KERNEL32.DLL"), image_a,
                            MAX_PATH) ||
        !MultiByteToWideChar(CP_ACP, 0, image_a, -1, image_w, MAX_PATH))
        fail("system KERNEL32 path conversion");
    SetLastError(ERROR_SUCCESS);
    search = FindFirstStreamW(image_w, FindStreamInfoStandard, &data, 0);
    error = GetLastError();
    if (search != INVALID_HANDLE_VALUE) {
        static const WCHAR expected[] = L"::$DATA";
        int i;
        for (i = 0; expected[i]; ++i)
            if (data.stream_name[i] != expected[i])
                fail("native first stream spelling");
        if (data.stream_name[i] || !FindClose(search))
            fail("native stream search handle");
    } else if (error != ERROR_INVALID_PARAMETER) {
        fail("unsupported filesystem error");
    }
    say("PASS: KERNEL32 static FindFirstStreamW import and behavior\r\n");
    ExitProcess(0);
}
