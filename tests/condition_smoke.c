/* Direct table test: real Win98-loadable condition-variable synchronization. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

#define SHARED_MODE 1
typedef struct test_condition { void *state; } test_condition;
typedef struct test_srwlock { volatile LONG state; } test_srwlock;
typedef const m98_api_table *(*get_table_fn)(void);
typedef void (WINAPI *condition_init_fn)(test_condition *);
typedef void (WINAPI *condition_wake_fn)(test_condition *);
typedef BOOL (WINAPI *condition_sleep_fn)(test_condition *, test_srwlock *, DWORD, ULONG);
typedef void (WINAPI *srw_fn)(test_srwlock *);
typedef BOOLEAN (WINAPI *try_srw_fn)(test_srwlock *);

typedef struct test_ops {
    condition_init_fn init;
    condition_wake_fn wake_one;
    condition_wake_fn wake_all;
    condition_sleep_fn sleep;
    srw_fn init_lock, acquire_exclusive, acquire_shared;
    srw_fn release_exclusive, release_shared;
    try_srw_fn try_exclusive;
} test_ops;

typedef struct worker_args {
    const test_ops *ops;
    test_condition *condition;
    test_srwlock *lock;
    HANDLE entered;
    DWORD milliseconds;
    ULONG flags;
    BOOL result, still_held;
    DWORD error;
} worker_args;

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

static BOOL same(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void *find_api(const m98_api_table *table, const char *name)
{
    int i;
    for (i = 0; i < table->named_apis_count; ++i)
        if (same(table->named_apis[i].name, name))
            return (void *)(ULONG_PTR)table->named_apis[i].addr;
    return 0;
}

static DWORD WINAPI condition_worker(void *opaque)
{
    worker_args *args = (worker_args *)opaque;
    if (args->flags & SHARED_MODE) args->ops->acquire_shared(args->lock);
    else args->ops->acquire_exclusive(args->lock);
    SetEvent(args->entered);
    args->result = args->ops->sleep(args->condition, args->lock,
                                    args->milliseconds, args->flags);
    args->error = GetLastError();
    /* An exclusive try must fail while this caller holds either mode. */
    args->still_held = !args->ops->try_exclusive(args->lock);
    if (!args->still_held) {
        args->ops->release_exclusive(args->lock);
        return 1;
    }
    if (args->flags & SHARED_MODE) args->ops->release_shared(args->lock);
    else args->ops->release_exclusive(args->lock);
    return 0;
}

static void init_worker(worker_args *args, const test_ops *ops,
                        test_condition *condition, test_srwlock *lock,
                        DWORD timeout, ULONG flags)
{
    args->ops = ops;
    args->condition = condition;
    args->lock = lock;
    args->entered = CreateEventA(0, TRUE, FALSE, 0);
    args->milliseconds = timeout;
    args->flags = flags;
    args->result = FALSE;
    args->still_held = FALSE;
    args->error = 0;
    if (!args->entered) fail("worker event");
}

static void wait_until_released(test_ops *ops, test_srwlock *lock,
                                HANDLE entered)
{
    DWORD start = GetTickCount();
    if (WaitForSingleObject(entered, 3000) != WAIT_OBJECT_0)
        fail("worker acquired initial SRW lock");
    while (!ops->try_exclusive(lock)) {
        if (GetTickCount() - start > 3000)
            fail("condition sleep releases SRW lock");
        Sleep(1);
    }
}

static void close_worker(worker_args *args, HANDLE thread)
{
    CloseHandle(thread);
    CloseHandle(args->entered);
}

void mainCRTStartup(void)
{
    HMODULE dll = LoadLibraryA("m98wrap.dll");
    get_table_fn get_table;
    const m98_api_table *table;
    test_ops ops;
    test_condition condition = { 0 }, other = { 0 };
    test_srwlock lock = { 0 };
    worker_args first, second;
    HANDLE thread_a, thread_b, threads[2];
    DWORD awakened;

    if (!dll) fail("LoadLibrary m98wrap.dll");
    get_table = (get_table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table");
    table = get_table();
    if (!table || !same(table->target_library, "KERNEL32.DLL") ||
        table->named_apis_count < 52) fail("condition KERNEL32 table");
    ops.init = (condition_init_fn)find_api(table, "InitializeConditionVariable");
    ops.wake_one = (condition_wake_fn)find_api(table, "WakeConditionVariable");
    ops.wake_all = (condition_wake_fn)find_api(table, "WakeAllConditionVariable");
    ops.sleep = (condition_sleep_fn)find_api(table, "SleepConditionVariableSRW");
    ops.init_lock = (srw_fn)find_api(table, "InitializeSRWLock");
    ops.acquire_exclusive = (srw_fn)find_api(table, "AcquireSRWLockExclusive");
    ops.acquire_shared = (srw_fn)find_api(table, "AcquireSRWLockShared");
    ops.release_exclusive = (srw_fn)find_api(table, "ReleaseSRWLockExclusive");
    ops.release_shared = (srw_fn)find_api(table, "ReleaseSRWLockShared");
    ops.try_exclusive = (try_srw_fn)find_api(table, "TryAcquireSRWLockExclusive");
    if (!ops.init || !ops.wake_one || !ops.wake_all || !ops.sleep ||
        !ops.init_lock || !ops.acquire_exclusive || !ops.acquire_shared ||
        !ops.release_exclusive || !ops.release_shared || !ops.try_exclusive)
        fail("condition and SRW API table pointers");
    condition.state = (void *)1;
    ops.init(&condition);
    ops.init(&other);
    ops.init_lock(&lock);
    if (condition.state || other.state || lock.state)
        fail("one-pointer static initialization");

    /* A wake before a wait is not a stored semaphore credit. Even a zero
     * timeout releases and reacquires the exclusive SRW lock. */
    ops.wake_all(&condition);
    ops.acquire_exclusive(&lock);
    SetLastError(ERROR_GEN_FAILURE);
    if (ops.sleep(&condition, &lock, 0, 0) ||
        GetLastError() != ERROR_TIMEOUT || ops.try_exclusive(&lock))
        fail("empty zero-timeout wait and exclusive reacquisition");
    ops.release_exclusive(&lock);
    if (lock.state) fail("zero-timeout lock release");

    /* A finite timeout must not return until the original lock can be
     * reacquired, even after the timeout interval has elapsed. */
    init_worker(&first, &ops, &condition, &lock, 80, 0);
    thread_a = CreateThread(0, 0, condition_worker, &first, 0, 0);
    if (!thread_a) fail("exclusive timeout thread");
    wait_until_released(&ops, &lock, first.entered);
    Sleep(130);
    if (WaitForSingleObject(thread_a, 30) != WAIT_TIMEOUT)
        fail("timed-out waiter reacquires before returning");
    ops.release_exclusive(&lock);
    if (WaitForSingleObject(thread_a, 3000) != WAIT_OBJECT_0 || first.result ||
        first.error != ERROR_TIMEOUT || !first.still_held || lock.state)
        fail("exclusive timeout result and lock restoration");
    close_worker(&first, thread_a);

    /* Multiple waiters are actually blocked; one wake releases exactly one
     * and leaves the other queued until wake-all. */
    init_worker(&first, &ops, &condition, &lock, INFINITE, 0);
    init_worker(&second, &ops, &condition, &lock, INFINITE, 0);
    thread_a = CreateThread(0, 0, condition_worker, &first, 0, 0);
    thread_b = CreateThread(0, 0, condition_worker, &second, 0, 0);
    if (!thread_a || !thread_b) fail("two exclusive waiter threads");
    if (WaitForSingleObject(first.entered, 3000) != WAIT_OBJECT_0 ||
        WaitForSingleObject(second.entered, 3000) != WAIT_OBJECT_0)
        fail("both exclusive waiters entered");
    /* Both entered before acquiring this lock, so neither can still own it. */
    wait_until_released(&ops, &lock, first.entered);
    ops.wake_one(&condition);
    ops.release_exclusive(&lock);
    threads[0] = thread_a;
    threads[1] = thread_b;
    awakened = WaitForMultipleObjects(2, threads, FALSE, 3000);
    if (awakened != WAIT_OBJECT_0 && awakened != WAIT_OBJECT_0 + 1)
        fail("wake-one releases a waiter");
    if (WaitForSingleObject(threads[1 - (awakened - WAIT_OBJECT_0)], 80) !=
        WAIT_TIMEOUT) fail("wake-one leaves the second waiter asleep");
    ops.wake_all(&condition);
    if (WaitForSingleObject(thread_a, 3000) != WAIT_OBJECT_0 ||
        WaitForSingleObject(thread_b, 3000) != WAIT_OBJECT_0 ||
        !first.result || !second.result || !first.still_held ||
        !second.still_held || lock.state)
        fail("wake-all completes both exclusive waits with locks");
    close_worker(&first, thread_a);
    close_worker(&second, thread_b);

    /* Shared mode and separate condition-variable addresses are distinct. */
    init_worker(&first, &ops, &condition, &lock, INFINITE, SHARED_MODE);
    thread_a = CreateThread(0, 0, condition_worker, &first, 0, 0);
    if (!thread_a) fail("shared waiter thread");
    wait_until_released(&ops, &lock, first.entered);
    ops.wake_all(&other);
    ops.release_exclusive(&lock);
    if (WaitForSingleObject(thread_a, 80) != WAIT_TIMEOUT)
        fail("unrelated condition does not wake shared waiter");
    ops.acquire_exclusive(&lock);
    ops.wake_all(&condition);
    if (WaitForSingleObject(thread_a, 50) != WAIT_TIMEOUT)
        fail("shared waiter waits to reacquire after wake-all");
    ops.release_exclusive(&lock);
    if (WaitForSingleObject(thread_a, 3000) != WAIT_OBJECT_0 ||
        !first.result || !first.still_held || lock.state)
        fail("shared-mode wake and lock restoration");
    close_worker(&first, thread_a);

    say("PASS: condition variable wake-one/wake-all, shared/exclusive and timeout\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
