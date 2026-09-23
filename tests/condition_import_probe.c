/* Guest-only static KERNEL32 imports; the original Win98 DLL lacks these. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

typedef struct test_condition { void *state; } test_condition;
typedef struct test_srwlock { volatile LONG state; } test_srwlock;

__declspec(dllimport) void WINAPI InitializeConditionVariable(test_condition *);
__declspec(dllimport) BOOL WINAPI SleepConditionVariableSRW(
    test_condition *, test_srwlock *, DWORD, ULONG);
__declspec(dllimport) void WINAPI WakeConditionVariable(test_condition *);
__declspec(dllimport) void WINAPI WakeAllConditionVariable(test_condition *);
__declspec(dllimport) void WINAPI InitializeSRWLock(test_srwlock *);
__declspec(dllimport) void WINAPI AcquireSRWLockExclusive(test_srwlock *);
__declspec(dllimport) void WINAPI ReleaseSRWLockExclusive(test_srwlock *);

static void say(const char *message)
{
    DWORD size = 0, written;
    while (message[size]) ++size;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, size, &written, 0);
}

void mainCRTStartup(void)
{
    test_condition condition = { 0 };
    test_srwlock lock = { 0 };
    InitializeConditionVariable(&condition);
    InitializeSRWLock(&lock);
    WakeConditionVariable(&condition);
    WakeAllConditionVariable(&condition);
    AcquireSRWLockExclusive(&lock);
    if (condition.state || SleepConditionVariableSRW(&condition, &lock, 0, 0) ||
        GetLastError() != ERROR_TIMEOUT) {
        say("FAIL: static KERNEL32 condition-variable imports\r\n");
        ExitProcess(1);
    }
    ReleaseSRWLockExclusive(&lock);
    say("PASS: static KERNEL32 condition-variable imports\r\n");
    ExitProcess(0);
}
