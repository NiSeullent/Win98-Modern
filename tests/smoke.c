/* Win98-loadable smoke test: uses only KERNEL32 imports and no C runtime. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

typedef const m98_api_table *(*get_api_table_fn)(void);
typedef DWORD (WINAPI *count_fn)(WORD);
typedef ULONGLONG (WINAPI *tick64_fn)(void);
typedef BOOL (WINAPI *init_cs_fn)(LPCRITICAL_SECTION, DWORD, DWORD);
typedef BOOL (WINAPI *file_info_fn)(HANDLE, int, LPVOID, DWORD);
typedef int (WINAPI *compare_ordinal_fn)(const WCHAR *, int, const WCHAR *, int, BOOL);
typedef int (WINAPI *compare_ex_fn)(const WCHAR *, DWORD, const WCHAR *, int,
                                    const WCHAR *, int, void *, void *, LPARAM);
typedef int (WINAPI *map_ex_fn)(const WCHAR *, DWORD, const WCHAR *, int,
                                WCHAR *, int, void *, void *, LPARAM);
typedef LONG (WINAPI *package_fn)(UINT32 *, BYTE *);
typedef LONG (WINAPI *package_name_fn)(UINT32 *, WCHAR *);
typedef LONG (WINAPI *package_info_fn)(UINT32, UINT32 *, BYTE *, UINT32 *);
typedef LONG (WINAPI *process_package_name_fn)(HANDLE, UINT32 *, WCHAR *);
typedef LONG (WINAPI *process_package_id_fn)(HANDLE, UINT32 *, BYTE *);
typedef BOOL (WINAPI *process_group_fn)(HANDLE, PUSHORT, PUSHORT);
typedef BOOL (WINAPI *firmware_fn)(DWORD *);
typedef DWORD (WINAPI *dep_policy_fn)(void);
typedef BOOL (WINAPI *numa_node_fn)(UCHAR, PUCHAR);
typedef BOOL (WINAPI *numa_available_fn)(UCHAR, PULONGLONG);
typedef BOOL (WINAPI *numa_available_ex_fn)(USHORT, PULONGLONG);
typedef BOOL (WINAPI *numa_mask_ex_fn)(USHORT, void *);
typedef BOOL (WINAPI *numa_node_ex_fn)(const void *, PUSHORT);
typedef void (WINAPI *precise_time_fn)(LPFILETIME);
typedef HRESULT (WINAPI *restart_settings_fn)(HANDLE, WCHAR *, DWORD *, DWORD *);
typedef HRESULT (WINAPI *register_restart_fn)(const WCHAR *, DWORD);
typedef HRESULT (WINAPI *unregister_restart_fn)(void);
typedef BOOL (WINAPI *process_path_w_fn)(HANDLE, DWORD, WCHAR *, DWORD *);
typedef struct test_srwlock { volatile LONG state; } test_srwlock;
typedef struct test_condition { void *state; } test_condition;
typedef struct test_init_once { volatile LONG state; } test_init_once;
typedef void (WINAPI *srw_fn)(test_srwlock *);
typedef BOOLEAN (WINAPI *try_srw_fn)(test_srwlock *);
typedef void (WINAPI *condition_init_fn)(test_condition *);
typedef void (WINAPI *condition_wake_fn)(test_condition *);
typedef BOOL (WINAPI *condition_sleep_fn)(test_condition *, test_srwlock *,
                                          DWORD, ULONG);
typedef BOOL (WINAPI *init_once_callback_fn)(test_init_once *, void *, void **);
typedef BOOL (WINAPI *init_once_fn)(test_init_once *, init_once_callback_fn,
                                    void *, void **);

typedef struct srw_worker_args {
    test_srwlock *lock;
    srw_fn acquire;
    srw_fn release;
    HANDLE entered;
    HANDLE release_event;
} srw_worker_args;

typedef struct once_worker_args {
    test_init_once *once;
    init_once_fn execute;
    HANDLE started;
    void *context;
    BOOL success;
} once_worker_args;

static DWORD WINAPI srw_worker(void *opaque)
{
    srw_worker_args *args = (srw_worker_args *)opaque;
    args->acquire(args->lock);
    SetEvent(args->entered);
    if (args->release_event) WaitForSingleObject(args->release_event, INFINITE);
    args->release(args->lock);
    return 0;
}

static DWORD init_once_calls;
static DWORD init_once_value;
static HANDLE once_callback_entered, once_callback_release;
static volatile LONG once_concurrent_calls;

static BOOL WINAPI init_once_callback(test_init_once *once, void *parameter,
                                      void **context)
{
    DWORD *fail_first = (DWORD *)parameter;
    (void)once;
    init_once_calls++;
    if (*fail_first) {
        *fail_first = 0;
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    *context = &init_once_value;
    return TRUE;
}

static BOOL WINAPI init_once_no_context(test_init_once *once, void *parameter,
                                        void **context)
{
    DWORD *calls = (DWORD *)parameter;
    (void)once;
    if (context) return FALSE;
    ++*calls;
    return TRUE;
}

static BOOL WINAPI init_once_blocking_callback(test_init_once *once,
                                                void *parameter, void **context)
{
    (void)once;
    (void)parameter;
    InterlockedIncrement(&once_concurrent_calls);
    SetEvent(once_callback_entered);
    if (WaitForSingleObject(once_callback_release, 4000) != WAIT_OBJECT_0) {
        SetLastError(ERROR_GEN_FAILURE);
        return FALSE;
    }
    *context = &init_once_value;
    return TRUE;
}

static DWORD WINAPI once_worker(void *opaque)
{
    once_worker_args *args = (once_worker_args *)opaque;
    SetEvent(args->started);
    args->success = args->execute(args->once, init_once_blocking_callback,
                                  0, &args->context);
    return 0;
}

typedef struct test_processor_number {
    WORD Group;
    BYTE Number;
    BYTE Reserved;
} test_processor_number;

typedef struct test_group_affinity {
    ULONG_PTR Mask;
    WORD Group;
    WORD Reserved[3];
} test_group_affinity;

typedef struct test_file_standard_info {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    DWORD NumberOfLinks;
    BYTE DeletePending;
    BYTE Directory;
} test_file_standard_info;

typedef struct test_file_basic_info {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    DWORD FileAttributes;
} test_file_basic_info;

static void report(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, 0);
}

static void fail(const char *message)
{
    report("FAIL: ");
    report(message);
    report("\r\n");
    ExitProcess(1);
}

static int compare(const char *left, const char *right)
{
    while (*left && *left == *right) { left++; right++; }
    return (unsigned char)*left - (unsigned char)*right;
}

static unsigned long find_api(const m98_api_table *table, const char *name)
{
    int i;
    for (i = 0; i < table->named_apis_count; ++i)
        if (compare(table->named_apis[i].name, name) == 0)
            return table->named_apis[i].addr;
    return 0;
}

void mainCRTStartup(void)
{
    HMODULE dll = LoadLibraryA("m98wrap.dll");
    get_api_table_fn get_table;
    const m98_api_table *table;
    int i;
    count_fn active, maximum;
    tick64_fn tick;
    init_cs_fn init_cs;
    file_info_fn file_info;
    compare_ordinal_fn compare_ordinal;
    compare_ex_fn compare_ex;
    map_ex_fn map_ex;
    package_fn get_package_id;
    package_name_fn get_current_package_family, get_current_package_full;
    package_name_fn get_current_package_path;
    package_info_fn get_package_info;
    process_package_name_fn get_package_family, get_package_full;
    process_package_id_fn get_process_package_id;
    process_group_fn get_process_group;
    firmware_fn get_firmware;
    dep_policy_fn get_dep_policy;
    numa_node_fn get_numa_node;
    numa_available_fn numa_available;
    numa_available_ex_fn numa_available_ex;
    numa_mask_ex_fn numa_mask_ex;
    numa_node_ex_fn numa_node_ex;
    precise_time_fn precise_time;
    restart_settings_fn get_restart_settings;
    register_restart_fn register_restart;
    unregister_restart_fn unregister_restart;
    process_path_w_fn process_path_w;
    srw_fn init_srw, acquire_exclusive, acquire_shared;
    srw_fn release_exclusive, release_shared;
    try_srw_fn try_exclusive, try_shared;
    condition_init_fn init_condition;
    condition_wake_fn wake_condition, wake_all_conditions;
    condition_sleep_fn sleep_condition;
    init_once_fn init_once_execute;
    test_srwlock srw;
    test_condition condition = { 0 };
    test_init_once once = { 0 }, once_without_context = { 0 };
    test_init_once concurrent_once = { 0 };
    srw_worker_args worker_args;
    HANDLE worker, entered, release_event;
    once_worker_args once_worker_a, once_worker_b;
    HANDLE once_thread_a, once_thread_b, once_worker_started;
    DWORD fail_first = 1, no_context_calls = 0, wait_started;
    void *once_context = (void *)1;
    CRITICAL_SECTION cs;
    ULONGLONG first, second;
    ULONGLONG free_bytes;
    HANDLE file;
    test_file_standard_info info;
    test_file_basic_info basic;
    BY_HANDLE_FILE_INFORMATION original_info;
    DWORD old_size;
    DWORD module_path_length;
    char module_path[MAX_PATH];
    DWORD firmware;
    WORD group_count, group_array[2];
    UINT32 package_length, package_count;
    DWORD exit_code, invalid_process_error;
    DWORD restart_size = 2, restart_flags = 0x12345678;
    WCHAR restart_line[2] = { L'X', 0 };
    UCHAR node;
    USHORT node_ex;
    test_processor_number processor;
    test_group_affinity affinity;
    FILETIME clock_time;
    const WCHAR embedded_left[3] = { 'a', 0, 'b' };
    const WCHAR embedded_right[3] = { 'a', 0, 'c' };
    WCHAR mapped[4] = { 0, 0, 0, 0 };

    if (!dll) fail("LoadLibraryA(m98wrap.dll)");
    get_table = (get_api_table_fn)GetProcAddress(dll, "get_api_table");
    if (!get_table) fail("get_api_table export");
    table = get_table();
    if (!table || !table->target_library ||
        compare(table->target_library, "KERNEL32.DLL") != 0 ||
        table[1].target_library != 0 || table->named_apis_count != 89)
        fail("KernelEx table layout");
    for (i = 1; i < table->named_apis_count; ++i)
        if (compare(table->named_apis[i-1].name, table->named_apis[i].name) >= 0)
            fail("API table sort order");

    get_restart_settings = (restart_settings_fn)find_api(
        table, "GetApplicationRestartSettings");
    register_restart = (register_restart_fn)find_api(
        table, "RegisterApplicationRestart");
    unregister_restart = (unregister_restart_fn)find_api(
        table, "UnregisterApplicationRestart");
    if (!get_restart_settings || !register_restart || !unregister_restart ||
        get_restart_settings(GetCurrentProcess(), restart_line,
                             &restart_size, &restart_flags) !=
            HRESULT_FROM_WIN32(ERROR_NOT_FOUND) ||
        register_restart(L"/restart", 0) != E_FAIL ||
        unregister_restart() != S_OK)
        fail("unregistered application restart fallback");

    process_path_w = (process_path_w_fn)find_api(
        table, "QueryFullProcessImageNameW");
    if (!process_path_w || !find_api(table, "QueryFullProcessImageNameA"))
        fail("process path table entries");
    {
        WCHAR process_path[MAX_PATH];
        DWORD process_path_size = MAX_PATH;
        if (!process_path_w(GetCurrentProcess(), 0, process_path,
                            &process_path_size) || !process_path_size ||
            process_path[process_path_size] != 0)
            fail("current process path");
    }

    active = (count_fn)find_api(table, "GetActiveProcessorCount");
    maximum = (count_fn)find_api(table, "GetMaximumProcessorCount");
    if (!active || !maximum || active(0) != 1 || active(0xffff) != 1 ||
        active(1) != 0 || maximum(0) != 1)
        fail("processor count wrappers");
    if (GetLastError() != ERROR_INVALID_PARAMETER ||
        maximum(1) != 0 || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("processor group invalid parameter");
    get_numa_node = (numa_node_fn)find_api(table, "GetNumaProcessorNode");
    node = 0;
    if (!get_numa_node || get_numa_node(1, &node) ||
        node != 0xff || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("NUMA invalid processor");
    numa_available = (numa_available_fn)find_api(table, "GetNumaAvailableMemoryNode");
    numa_available_ex = (numa_available_ex_fn)find_api(table, "GetNumaAvailableMemoryNodeEx");
    numa_mask_ex = (numa_mask_ex_fn)find_api(table, "GetNumaNodeProcessorMaskEx");
    numa_node_ex = (numa_node_ex_fn)find_api(table, "GetNumaProcessorNodeEx");
    if (!numa_available || !numa_available_ex || !numa_mask_ex || !numa_node_ex ||
        !numa_available(0, &free_bytes) || free_bytes == 0 ||
        numa_available_ex(1, &free_bytes) || GetLastError() != ERROR_INVALID_PARAMETER ||
        !numa_mask_ex(0, &affinity) || affinity.Mask != 1 || affinity.Group != 0 ||
        numa_mask_ex(1, &affinity) || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("NUMA extended wrappers");
    processor.Group = 0;
    processor.Number = 0;
    processor.Reserved = 0;
    if (!numa_node_ex(&processor, &node_ex) || node_ex != 0)
        fail("NUMA processor node Ex");
    processor.Number = 1;
    if (numa_node_ex(&processor, &node_ex) || node_ex != 0xffff ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("NUMA processor node Ex invalid");

    compare_ordinal = (compare_ordinal_fn)find_api(table, "CompareStringOrdinal");
    if (!compare_ordinal ||
        compare_ordinal(L"abc", -1, L"ABC", -1, TRUE) != CSTR_EQUAL ||
        compare_ordinal(L"\x03c3", -1, L"\x03a3", -1, TRUE) != CSTR_EQUAL ||
        compare_ordinal(L"a", -1, L"b", -1, FALSE) != CSTR_LESS_THAN ||
        compare_ordinal(embedded_left, 3, embedded_right, 3, FALSE) != CSTR_LESS_THAN ||
        compare_ordinal(embedded_left, -1, embedded_right, -1, FALSE) != CSTR_EQUAL ||
        compare_ordinal(0, -1, L"b", -1, FALSE) != 0 ||
        compare_ordinal(L"a", -2, L"A", -1, TRUE) != 0 ||
        GetLastError() != ERROR_INVALID_PARAMETER ||
        compare_ordinal(L"a", -1, L"A", -1, 2) != 0 ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("CompareStringOrdinal");

    compare_ex = (compare_ex_fn)find_api(table, "CompareStringEx");
    map_ex = (map_ex_fn)find_api(table, "LCMapStringEx");
    if (!compare_ex || !map_ex ||
        compare_ex(0, NORM_IGNORECASE, L"abc", -1, L"ABC", -1,
                   0, 0, 0) != CSTR_EQUAL ||
        map_ex(0, LCMAP_UPPERCASE, L"abc", -1, mapped, 4,
               0, 0, 0) != 4 ||
        mapped[0] != L'A' || mapped[1] != L'B' ||
        mapped[2] != L'C' || mapped[3] != 0)
        fail("locale-name comparison and mapping");

    get_package_id = (package_fn)find_api(table, "GetCurrentPackageId");
    get_current_package_family = (package_name_fn)find_api(table, "GetCurrentPackageFamilyName");
    get_current_package_full = (package_name_fn)find_api(table, "GetCurrentPackageFullName");
    get_current_package_path = (package_name_fn)find_api(table, "GetCurrentPackagePath");
    package_length = 0;
    if (!get_package_id || !get_current_package_family ||
        !get_current_package_full || !get_current_package_path ||
        get_package_id(&package_length, 0) != 15700 ||
        get_current_package_family(&package_length, 0) != 15700 ||
        get_current_package_full(&package_length, 0) != 15700 ||
        get_current_package_path(&package_length, 0) != 15700 ||
        get_package_id(0, 0) != ERROR_INVALID_PARAMETER ||
        get_current_package_family(0, 0) != ERROR_INVALID_PARAMETER ||
        get_current_package_full(0, 0) != ERROR_INVALID_PARAMETER ||
        get_current_package_path(0, 0) != ERROR_INVALID_PARAMETER)
        fail("current package identity wrappers");
    get_package_info = (package_info_fn)find_api(table, "GetCurrentPackageInfo");
    get_package_family = (process_package_name_fn)find_api(table, "GetPackageFamilyName");
    get_package_full = (process_package_name_fn)find_api(table, "GetPackageFullName");
    get_process_package_id = (process_package_id_fn)find_api(table, "GetPackageId");
    package_length = 0;
    package_count = 0;
    if (GetExitCodeProcess((HANDLE)0x5badf00d, &exit_code))
        fail("invalid process handle unexpectedly accepted");
    invalid_process_error = GetLastError();
    if (!get_package_info || !get_package_family || !get_package_full ||
        !get_process_package_id ||
        get_package_info(0, &package_length, 0, &package_count) != 15700 ||
        get_package_family(GetCurrentProcess(), &package_length, 0) != 15700 ||
        get_package_full(GetCurrentProcess(), &package_length, 0) != 15700 ||
        get_process_package_id(GetCurrentProcess(), &package_length, 0) != 15700 ||
        get_package_info(0, 0, 0, &package_count) != ERROR_INVALID_PARAMETER ||
        get_package_family(GetCurrentProcess(), 0, 0) != ERROR_INVALID_PARAMETER ||
        get_package_full(GetCurrentProcess(), 0, 0) != ERROR_INVALID_PARAMETER ||
        get_process_package_id(GetCurrentProcess(), 0, 0) != ERROR_INVALID_PARAMETER ||
        get_process_package_id(0, &package_length, 0) != ERROR_INVALID_PARAMETER ||
        get_process_package_id((HANDLE)0x5badf00d, &package_length, 0) !=
            (LONG)invalid_process_error)
        fail("package identity wrappers");

    get_process_group = (process_group_fn)find_api(table, "GetProcessGroupAffinity");
    group_count = 0;
    if (!get_process_group ||
        get_process_group(GetCurrentProcess(), &group_count, 0) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || group_count != 1)
        fail("process group size query");
    group_count = 2;
    group_array[0] = 9;
    group_array[1] = 9;
    if (!get_process_group(GetCurrentProcess(), &group_count, group_array) ||
        group_count != 1 || group_array[0] != 0 || group_array[1] != 9 ||
        get_process_group(0, &group_count, group_array) ||
        GetLastError() != ERROR_INVALID_HANDLE)
        fail("process group query");

    get_firmware = (firmware_fn)find_api(table, "GetFirmwareType");
    get_dep_policy = (dep_policy_fn)find_api(table, "GetSystemDEPPolicy");
    firmware = 0;
    if (!get_firmware || !get_dep_policy || !get_firmware(&firmware) ||
        firmware != 1 || get_firmware(0) ||
        GetLastError() != ERROR_INVALID_PARAMETER || get_dep_policy() != 0)
        fail("firmware and DEP reports");

    init_cs = (init_cs_fn)find_api(table, "InitializeCriticalSectionEx");
    if (!init_cs || !init_cs(&cs, 0, 0)) fail("critical section init");
    EnterCriticalSection(&cs);
    LeaveCriticalSection(&cs);
    DeleteCriticalSection(&cs);
    if (init_cs(&cs, 0, 1) || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("critical section invalid flags");

    init_srw = (srw_fn)find_api(table, "InitializeSRWLock");
    acquire_exclusive = (srw_fn)find_api(table, "AcquireSRWLockExclusive");
    acquire_shared = (srw_fn)find_api(table, "AcquireSRWLockShared");
    release_exclusive = (srw_fn)find_api(table, "ReleaseSRWLockExclusive");
    release_shared = (srw_fn)find_api(table, "ReleaseSRWLockShared");
    try_exclusive = (try_srw_fn)find_api(table, "TryAcquireSRWLockExclusive");
    try_shared = (try_srw_fn)find_api(table, "TryAcquireSRWLockShared");
    if (!init_srw || !acquire_exclusive || !acquire_shared ||
        !release_exclusive || !release_shared || !try_exclusive || !try_shared)
        fail("SRW lock API pointers");
    srw.state = 123;
    init_srw(&srw);
    if (srw.state != 0 || !try_shared(&srw) || !try_shared(&srw) ||
        try_exclusive(&srw))
        fail("SRW shared and exclusive exclusion");
    release_shared(&srw);
    release_shared(&srw);
    if (!try_exclusive(&srw) || try_shared(&srw) || try_exclusive(&srw))
        fail("SRW exclusive and shared exclusion");
    release_exclusive(&srw);
    entered = CreateEventA(0, TRUE, FALSE, 0);
    release_event = CreateEventA(0, TRUE, FALSE, 0);
    if (!entered || !release_event) fail("SRW worker events");
    worker_args.lock = &srw;
    worker_args.acquire = acquire_shared;
    worker_args.release = release_shared;
    worker_args.entered = entered;
    worker_args.release_event = release_event;
    acquire_shared(&srw);
    worker = CreateThread(0, 0, srw_worker, &worker_args, 0, 0);
    if (!worker || WaitForSingleObject(entered, 2000) != WAIT_OBJECT_0 ||
        try_exclusive(&srw))
        fail("SRW concurrent readers");
    release_shared(&srw);
    SetEvent(release_event);
    if (WaitForSingleObject(worker, 2000) != WAIT_OBJECT_0 || srw.state != 0)
        fail("SRW reader release");
    CloseHandle(worker);
    ResetEvent(entered);
    acquire_exclusive(&srw);
    worker = CreateThread(0, 0, srw_worker, &worker_args, 0, 0);
    if (!worker || WaitForSingleObject(entered, 30) != WAIT_TIMEOUT)
        fail("SRW exclusive blocks reader");
    release_exclusive(&srw);
    if (WaitForSingleObject(worker, 2000) != WAIT_OBJECT_0 || srw.state != 0)
        fail("SRW reader wakes after exclusive release");
    CloseHandle(worker);

    ResetEvent(entered);
    ResetEvent(release_event);
    worker_args.acquire = acquire_exclusive;
    worker_args.release = release_exclusive;
    acquire_shared(&srw);
    worker = CreateThread(0, 0, srw_worker, &worker_args, 0, 0);
    if (!worker) fail("SRW writer thread");
    wait_started = GetTickCount();
    while (try_shared(&srw)) {
        release_shared(&srw);
        if (GetTickCount() - wait_started > 2000)
            fail("SRW writer registration");
        Sleep(1);
    }
    if (try_exclusive(&srw) || WaitForSingleObject(entered, 30) != WAIT_TIMEOUT)
        fail("SRW writer waits for reader");
    release_shared(&srw);
    if (WaitForSingleObject(entered, 2000) != WAIT_OBJECT_0 ||
        try_shared(&srw))
        fail("SRW writer acquires after reader");
    SetEvent(release_event);
    if (WaitForSingleObject(worker, 2000) != WAIT_OBJECT_0 || srw.state != 0)
        fail("SRW writer release");
    CloseHandle(worker);
    CloseHandle(entered);
    CloseHandle(release_event);

    init_condition = (condition_init_fn)find_api(
        table, "InitializeConditionVariable");
    sleep_condition = (condition_sleep_fn)find_api(
        table, "SleepConditionVariableSRW");
    wake_condition = (condition_wake_fn)find_api(
        table, "WakeConditionVariable");
    wake_all_conditions = (condition_wake_fn)find_api(
        table, "WakeAllConditionVariable");
    if (!init_condition || !sleep_condition || !wake_condition ||
        !wake_all_conditions) fail("condition-variable API pointers");
    init_condition(&condition);
    wake_condition(&condition);
    wake_all_conditions(&condition);
    acquire_exclusive(&srw);
    if (sleep_condition(&condition, &srw, 0, 0) ||
        GetLastError() != ERROR_TIMEOUT || try_exclusive(&srw))
        fail("empty condition-variable timeout and lock reacquisition");
    release_exclusive(&srw);

    init_once_execute = (init_once_fn)find_api(table, "InitOnceExecuteOnce");
    if (!init_once_execute ||
        init_once_execute(&once, init_once_callback, &fail_first, &once_context) ||
        GetLastError() != ERROR_GEN_FAILURE || init_once_calls != 1 ||
        once_context != (void *)1 ||
        !init_once_execute(&once, init_once_callback, &fail_first, &once_context) ||
        init_once_calls != 2 || once_context != &init_once_value ||
        !init_once_execute(&once, init_once_callback, &fail_first, &once_context) ||
        init_once_calls != 2 || once_context != &init_once_value ||
        init_once_execute(0, init_once_callback, &fail_first, &once_context) ||
        GetLastError() != ERROR_INVALID_PARAMETER)
        fail("InitOnceExecuteOnce callback retry and caching");
    if (!init_once_execute(&once_without_context, init_once_no_context,
                           &no_context_calls, 0) ||
        !init_once_execute(&once_without_context, init_once_no_context,
                           &no_context_calls, 0) || no_context_calls != 1)
        fail("InitOnceExecuteOnce optional context");
    once_callback_entered = CreateEventA(0, TRUE, FALSE, 0);
    once_callback_release = CreateEventA(0, TRUE, FALSE, 0);
    once_worker_started = CreateEventA(0, TRUE, FALSE, 0);
    if (!once_callback_entered || !once_callback_release ||
        !once_worker_started)
        fail("InitOnceExecuteOnce worker events");
    once_worker_a.once = &concurrent_once;
    once_worker_a.execute = init_once_execute;
    once_worker_a.started = once_worker_started;
    once_worker_a.context = 0;
    once_worker_a.success = FALSE;
    once_worker_b = once_worker_a;
    once_thread_a = CreateThread(0, 0, once_worker, &once_worker_a, 0, 0);
    if (!once_thread_a ||
        WaitForSingleObject(once_callback_entered, 2000) != WAIT_OBJECT_0)
        fail("InitOnceExecuteOnce first callback");
    ResetEvent(once_worker_started);
    once_thread_b = CreateThread(0, 0, once_worker, &once_worker_b, 0, 0);
    if (!once_thread_b ||
        WaitForSingleObject(once_worker_started, 2000) != WAIT_OBJECT_0 ||
        WaitForSingleObject(once_thread_b, 30) != WAIT_TIMEOUT ||
        once_concurrent_calls != 1)
        fail("InitOnceExecuteOnce concurrent callback exclusion");
    SetEvent(once_callback_release);
    if (WaitForSingleObject(once_thread_a, 2000) != WAIT_OBJECT_0 ||
        WaitForSingleObject(once_thread_b, 2000) != WAIT_OBJECT_0 ||
        !once_worker_a.success || !once_worker_b.success ||
        once_worker_a.context != &init_once_value ||
        once_worker_b.context != &init_once_value ||
        once_concurrent_calls != 1)
        fail("InitOnceExecuteOnce concurrent completion");
    CloseHandle(once_thread_a);
    CloseHandle(once_thread_b);
    CloseHandle(once_callback_entered);
    CloseHandle(once_callback_release);
    CloseHandle(once_worker_started);

    tick = (tick64_fn)find_api(table, "GetTickCount64");
    if (!tick) fail("GetTickCount64 pointer");
    first = tick();
    Sleep(20);
    second = tick();
    if (second < first || second - first > 2000)
        fail("GetTickCount64 monotonicity");
    precise_time = (precise_time_fn)find_api(table, "GetSystemTimePreciseAsFileTime");
    if (!precise_time) fail("precise-time API pointer");
    precise_time(&clock_time);
    if (clock_time.dwHighDateTime == 0 && clock_time.dwLowDateTime == 0)
        fail("precise-time fallback");

    file_info = (file_info_fn)find_api(table, "GetFileInformationByHandleEx");
    if (!file_info) fail("GetFileInformationByHandleEx pointer");
    module_path_length = GetModuleFileNameA(0, module_path, sizeof(module_path));
    if (!module_path_length || module_path_length >= sizeof(module_path))
        fail("locate smoke.exe");
    file = CreateFileA(module_path, GENERIC_READ, FILE_SHARE_READ, 0,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) fail("open smoke.exe");
    old_size = GetFileSize(file, 0);
    if (!GetFileInformationByHandle(file, &original_info))
        fail("native file info");
    if (!file_info(file, 0, &basic, sizeof(basic)) ||
        basic.CreationTime.LowPart != original_info.ftCreationTime.dwLowDateTime ||
        (DWORD)basic.CreationTime.HighPart != original_info.ftCreationTime.dwHighDateTime ||
        basic.LastAccessTime.LowPart != original_info.ftLastAccessTime.dwLowDateTime ||
        (DWORD)basic.LastAccessTime.HighPart != original_info.ftLastAccessTime.dwHighDateTime ||
        basic.LastWriteTime.LowPart != original_info.ftLastWriteTime.dwLowDateTime ||
        (DWORD)basic.LastWriteTime.HighPart != original_info.ftLastWriteTime.dwHighDateTime ||
        basic.ChangeTime.QuadPart != basic.LastWriteTime.QuadPart ||
        basic.FileAttributes != original_info.dwFileAttributes)
        fail("file basic info");
    if (!file_info(file, 1, &info, sizeof(info)) ||
        info.EndOfFile.HighPart != 0 || info.EndOfFile.LowPart != old_size)
        fail("file standard info");
    if (file_info(file, 1, &info, 1) || GetLastError() != ERROR_BAD_LENGTH)
        fail("file info short buffer");
    if (file_info(file, -1, &info, sizeof(info)) ||
        GetLastError() != ERROR_INVALID_PARAMETER ||
        file_info(file, 3, &info, sizeof(info)) ||
        GetLastError() != ERROR_INVALID_PARAMETER ||
        file_info(file, 25, &info, sizeof(info)) ||
        GetLastError() != ERROR_INVALID_PARAMETER ||
        file_info(file, 2, &info, sizeof(info)) ||
        GetLastError() != ERROR_NOT_SUPPORTED)
        fail("file info class errors");
    if (file_info(INVALID_HANDLE_VALUE, 1, &info, sizeof(info)) ||
        GetLastError() != ERROR_INVALID_HANDLE)
        fail("file info invalid handle");
    CloseHandle(file);

    report("PASS: 89-entry KernelEx table and sampled API behavior\r\n");
    FreeLibrary(dll);
    ExitProcess(0);
}
