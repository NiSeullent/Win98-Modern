/* Direct FLSFIX.DLL table test; safe to run on the host and installed Win98.
 * Native host FLS is queried dynamically and is never a static PE import. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"
#include "../src/m98_fls.h"

typedef const m98_api_table *(*table_fn)(void);
typedef DWORD (WINAPI *alloc_fn)(M98_FLS_CALLBACK);
typedef BOOL (WINAPI *free_fn)(DWORD);
typedef PVOID (WINAPI *get_fn)(DWORD);
typedef BOOL (WINAPI *set_fn)(DWORD, PVOID);
typedef LPVOID (WINAPI *fiber_create_fn)(SIZE_T, LPFIBER_START_ROUTINE, LPVOID);
typedef LPVOID (WINAPI *fiber_convert_fn)(LPVOID);
typedef VOID (WINAPI *fiber_switch_fn)(LPVOID);
typedef VOID (WINAPI *fiber_delete_fn)(LPVOID);
typedef HANDLE (WINAPI *thread_create_fn)(LPSECURITY_ATTRIBUTES, SIZE_T,
                                           LPTHREAD_START_ROUTINE, LPVOID,
                                           DWORD, LPDWORD);
typedef VOID (WINAPI *thread_exit_fn)(DWORD);
typedef VOID (WINAPI *free_exit_fn)(HMODULE, DWORD);

static alloc_fn bridge_alloc;
static free_fn bridge_free;
static get_fn bridge_get;
static set_fn bridge_set;
static fiber_create_fn bridge_create_fiber;
static fiber_convert_fn bridge_convert;
static fiber_switch_fn bridge_switch;
static fiber_delete_fn bridge_delete;
static thread_create_fn bridge_create_thread;
static thread_exit_fn bridge_exit;
static free_exit_fn bridge_free_exit;
static DWORD slot, callback_count, reentry_errors, temporary_slot = FLS_OUT_OF_INDEXES;
static BOOL test_reentry;
static LPVOID main_fiber;
static HMODULE fixture;
static HANDLE live_ready, live_release;
static int marker_main, marker_child, marker_natural, marker_explicit;
static int marker_free_exit, marker_delete, marker_parameter, marker_live;

static void say(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, 0);
}

static void fail(const char *message)
{
    say("FAIL: "); say(message); say("\r\n");
    ExitProcess(1);
}

static void hex32(const char *label, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char buffer[11];
    int i;
    say(label);
    buffer[0] = '0'; buffer[1] = 'x';
    for (i = 0; i < 8; ++i)
        buffer[2 + i] = digits[(value >> (28 - i * 4)) & 15];
    buffer[10] = 0;
    say(buffer); say("\r\n");
}

static BOOL equal(const char *left, const char *right)
{
    while (*left && *right) if (*left++ != *right++) return FALSE;
    return *left == *right;
}

static void *api(const m98_api_table *table, const char *name)
{
    int i;
    for (i = 0; i < table->named_apis_count; ++i)
        if (equal(table->named_apis[i].name, name))
            return (void *)table->named_apis[i].addr;
    fail("missing table API");
    return NULL;
}

static VOID WINAPI callback(PVOID value)
{
    if (!value) fail("callback with NULL value");
    InterlockedIncrement((volatile LONG *)&callback_count);
    if (test_reentry) {
        SetLastError(0);
        if (bridge_free(slot) || GetLastError() != ERROR_INVALID_PARAMETER)
            fail("retiring slot accepted reentrant free");
        ++reentry_errors;
        SetLastError(0);
        if (bridge_set(slot, value) || GetLastError() != ERROR_INVALID_PARAMETER)
            fail("retiring slot accepted reentrant set");
        ++reentry_errors;
        if (temporary_slot == FLS_OUT_OF_INDEXES) {
            temporary_slot = bridge_alloc(NULL);
            if (temporary_slot == FLS_OUT_OF_INDEXES || temporary_slot == slot)
                fail("retiring slot reused inside callback");
        }
    }
}

static VOID WINAPI child_fiber(LPVOID parameter)
{
    if (GetFiberData() != &marker_parameter || parameter != &marker_parameter)
        fail("CreateFiber changed native fiber parameter");
    SetLastError(123);
    if (bridge_get(slot) || GetLastError() != ERROR_SUCCESS)
        fail("new fiber did not start with empty value");
    if (!bridge_set(slot, &marker_child)) fail("set child value");
    if (bridge_get(slot) != &marker_child) fail("read child value");
    bridge_switch(main_fiber);
    fail("deleted child resumed");
}

static VOID WINAPI deletion_fiber(LPVOID parameter)
{
    if (parameter != &marker_parameter) fail("delete-fiber parameter");
    if (!bridge_set(slot, &marker_delete)) fail("set delete-fiber value");
    bridge_switch(main_fiber);
    fail("deleted fiber resumed");
}

static DWORD WINAPI natural_worker(LPVOID parameter)
{
    if (parameter != &marker_natural ||
        !bridge_set(slot, &marker_natural)) fail("natural thread FLS set");
    return 17;
}

static DWORD WINAPI live_worker(LPVOID parameter)
{
    if (parameter != &marker_live || !bridge_set(slot, &marker_live) ||
        !SetEvent(live_ready)) fail("live thread FLS set");
    if (WaitForSingleObject(live_release, 10000) != WAIT_OBJECT_0)
        fail("live thread release");
    SetLastError(0);
    if (bridge_get(slot) || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("FlsFree left live thread slot valid");
    return 29;
}

static DWORD WINAPI explicit_worker(LPVOID parameter)
{
    if (parameter != &marker_explicit ||
        !bridge_set(slot, &marker_explicit)) fail("explicit thread FLS set");
    bridge_exit(19);
    fail("ExitThread returned");
    return 0;
}

static DWORD WINAPI unload_worker(LPVOID parameter)
{
    if (parameter != &marker_free_exit ||
        !bridge_set(slot, &marker_free_exit)) fail("unload thread FLS set");
    bridge_free_exit(fixture, 23);
    fail("FreeLibraryAndExitThread returned");
    return 0;
}

static void join_worker(LPTHREAD_START_ROUTINE start, LPVOID parameter,
                        DWORD expected_exit)
{
    HANDLE thread;
    DWORD code = 0;
    thread = bridge_create_thread(NULL, 0, start, parameter, 0, NULL);
    if (!thread || WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0)
        fail("thread join");
    if (!GetExitCodeThread(thread, &code) || code != expected_exit)
        fail("thread exit code");
    if (!CloseHandle(thread)) fail("close thread handle");
}

static void native_host_comparison(const char *phase)
{
    HMODULE kernel = GetModuleHandleA("KERNEL32.DLL");
    alloc_fn native_alloc = (alloc_fn)GetProcAddress(kernel, "FlsAlloc");
    free_fn native_free = (free_fn)GetProcAddress(kernel, "FlsFree");
    get_fn native_get = (get_fn)GetProcAddress(kernel, "FlsGetValue");
    set_fn native_set = (set_fn)GetProcAddress(kernel, "FlsSetValue");
    DWORD native_slot;
    say("OBSERVED: native FLS comparison phase=");
    say(phase); say("\r\n");
    if (!native_alloc || !native_free || !native_get || !native_set) {
        say("OBSERVED: native Fls* absent; Win98 bridge-only contract\r\n");
        return;
    }
    native_slot = native_alloc(NULL);
    hex32("host_native_slot=", native_slot);
    if (native_slot == FLS_OUT_OF_INDEXES) fail("host native FlsAlloc");
    SetLastError(123);
    {
        PVOID empty = native_get(native_slot);
        DWORD error = GetLastError();
        if (empty || (error != ERROR_SUCCESS &&
                      error != ERROR_INVALID_PARAMETER))
            fail("host native empty FlsGetValue unexpected result");
        hex32("OBSERVED: host native first empty GetLastError=", error);
    }
    if (!native_set(native_slot, NULL)) fail("host native NULL FlsSetValue");
    SetLastError(123);
    if (native_get(native_slot) || GetLastError() != ERROR_SUCCESS)
        fail("host native explicitly empty FlsGetValue");
    if (!native_set(native_slot, &marker_main) ||
        native_get(native_slot) != &marker_main ||
        !native_free(native_slot)) fail("host native FLS simple contract");
    say("OBSERVED: host native FLS empty/set/get/free contract\r\n");
}

/* Regression cases use their own slots/callbacks, not the legacy count above. */
static DWORD rundown_a_slot, rundown_b_slot, rundown_reused_slot;
static volatile LONG rundown_b_calls, conversion_rejected;
static int rundown_a_value, rundown_b_value;
static VOID WINAPI rundown_b_callback(PVOID value)
{
    if(value!=&rundown_b_value) fail("rundown B value");
    InterlockedIncrement(&rundown_b_calls);
}
static VOID WINAPI rundown_a_callback(PVOID value)
{
    if(value!=&rundown_a_value) fail("rundown A value");
    /* Free B while this record is DYING. Its pending callback must be done
     * before a new generation can reuse the slot. */
    if(!bridge_free(rundown_b_slot)) fail("rundown callback frees pending B");
    if(rundown_b_calls!=1) fail("FlsFree returned before pending rundown B callback");
    rundown_reused_slot=bridge_alloc(NULL);
    if(rundown_reused_slot!=rundown_b_slot) fail("rundown callback B generation reuse");
}
static DWORD WINAPI rundown_slots_worker(LPVOID unused)
{
    (void)unused;
    if(!bridge_set(rundown_a_slot,&rundown_a_value)||
       !bridge_set(rundown_b_slot,&rundown_b_value)) fail("rundown worker values");
    return 0;
}
static void rundown_slot_regression(void)
{
    rundown_a_slot=bridge_alloc(rundown_a_callback);
    rundown_b_slot=bridge_alloc(rundown_b_callback);
    if(rundown_a_slot==FLS_OUT_OF_INDEXES||rundown_b_slot==FLS_OUT_OF_INDEXES)
        fail("rundown regression slots");
    join_worker(rundown_slots_worker,NULL,0);
    if(rundown_b_calls!=1) fail("old rundown generation callback repeated");
    if(!bridge_free(rundown_a_slot)||!bridge_free(rundown_reused_slot))
        fail("rundown regression free");
    say("PASS: FLS rundown callback free/reuse completes old generation first\r\n");
}
static VOID WINAPI conversion_callback(PVOID value)
{
    if(value!=&rundown_a_value) fail("conversion callback value");
    SetLastError(0);
    if(bridge_convert(NULL)||GetLastError()!=ERROR_INVALID_PARAMETER)
        fail("thread-to-fiber conversion accepted during thread rundown");
    InterlockedIncrement(&conversion_rejected);
}
static DWORD WINAPI conversion_exit_worker(LPVOID unused)
{
    (void)unused;
    if(!bridge_set(rundown_a_slot,&rundown_a_value)) fail("conversion exit value");
    return 0;
}
static void conversion_reentry_regression(void)
{
    rundown_a_slot=bridge_alloc(conversion_callback);
    if(rundown_a_slot==FLS_OUT_OF_INDEXES) fail("conversion regression slot");
    join_worker(conversion_exit_worker,NULL,0);
    if(conversion_rejected!=1||!bridge_free(rundown_a_slot)) fail("conversion rundown guard");
    say("PASS: FLS thread rundown rejects destructive conversion reentry\r\n");
}
static HANDLE delete_ready, delete_callback_entered, delete_callback_release;
static HANDLE delete_go, delete_entered;
static DWORD delete_slot, delete_guard_slot;
static int delete_value, delete_guard_value;
static VOID WINAPI blocking_free_callback(PVOID value)
{
    if(value!=&delete_value) fail("concurrent delete callback value");
    SetEvent(delete_callback_entered);
    if(WaitForSingleObject(delete_callback_release,10000)!=WAIT_OBJECT_0)
        fail("concurrent delete callback release");
}
static DWORD WINAPI self_delete_worker(LPVOID unused)
{
    LPVOID fiber;
    (void)unused;
    fiber=bridge_convert(NULL);
    if(!fiber||!bridge_set(delete_slot,&delete_value)) fail("self delete fiber setup");
    SetEvent(delete_ready);
    if(WaitForSingleObject(delete_go,10000)!=WAIT_OBJECT_0) fail("self delete start");
    SetEvent(delete_entered);
    bridge_delete(fiber);
    fail("DeleteFiber(current) returned while foreign FlsFree held a reference");
    return 1;
}
static DWORD WINAPI foreign_free_worker(LPVOID unused)
{
    (void)unused;
    if(!bridge_set(delete_guard_slot,&delete_guard_value)||!bridge_free(delete_slot))
        fail("foreign FlsFree");
    if(bridge_get(delete_guard_slot)!=&delete_guard_value)
        fail("foreign finalizer cleared another thread's TLS context");
    return 0;
}
static void current_delete_regression(void)
{
    HANDLE a,b; DWORD id;
    delete_ready=CreateEventA(NULL,TRUE,FALSE,NULL);
    delete_callback_entered=CreateEventA(NULL,TRUE,FALSE,NULL);
    delete_callback_release=CreateEventA(NULL,TRUE,FALSE,NULL);
    delete_go=CreateEventA(NULL,TRUE,FALSE,NULL);
    delete_entered=CreateEventA(NULL,TRUE,FALSE,NULL);
    if(!delete_ready||!delete_callback_entered||!delete_callback_release||
       !delete_go||!delete_entered) fail("delete regression events");
    delete_slot=bridge_alloc(blocking_free_callback); delete_guard_slot=bridge_alloc(NULL);
    if(delete_slot==FLS_OUT_OF_INDEXES||delete_guard_slot==FLS_OUT_OF_INDEXES)
        fail("delete regression slots");
    a=bridge_create_thread(NULL,0,self_delete_worker,NULL,0,&id);
    if(!a||WaitForSingleObject(delete_ready,10000)!=WAIT_OBJECT_0) fail("delete worker ready");
    b=bridge_create_thread(NULL,0,foreign_free_worker,NULL,0,&id);
    if(!b||WaitForSingleObject(delete_callback_entered,10000)!=WAIT_OBJECT_0)
        fail("foreign callback holds reference");
    SetEvent(delete_go);
    if(WaitForSingleObject(delete_entered,10000)!=WAIT_OBJECT_0) fail("current delete entered");
    if(WaitForSingleObject(a,100)!=WAIT_TIMEOUT)
        fail("current delete did not wait for foreign callback");
    SetEvent(delete_callback_release);
    if(WaitForSingleObject(a,10000)!=WAIT_OBJECT_0||
       WaitForSingleObject(b,10000)!=WAIT_OBJECT_0) fail("concurrent delete workers finish");
    CloseHandle(a); CloseHandle(b);
    if(!bridge_free(delete_guard_slot)) fail("delete guard slot free");
    CloseHandle(delete_ready); CloseHandle(delete_callback_entered);
    CloseHandle(delete_callback_release); CloseHandle(delete_go); CloseHandle(delete_entered);
    say("PASS: FLS current-fiber deletion waits and preserves foreign thread TLS\r\n");
}
static HANDLE convert_ready, convert_callback_entered, convert_begin, convert_freed;
static DWORD convert_slot, convert_guard_slot;
static VOID WINAPI concurrent_conversion_callback(PVOID value)
{
    if(value!=&rundown_a_value) fail("concurrent conversion callback value");
    SetEvent(convert_callback_entered);
    if(WaitForSingleObject(convert_begin,10000)!=WAIT_OBJECT_0)
        fail("concurrent conversion begin");
    Sleep(0); /* contend with native conversion and record ownership transfer */
}
static DWORD WINAPI concurrent_converter(LPVOID unused)
{
    (void)unused;
    if(!bridge_set(convert_slot,&rundown_a_value)||
       !bridge_set(convert_guard_slot,&rundown_b_value)) fail("concurrent conversion values");
    SetEvent(convert_ready);
    if(WaitForSingleObject(convert_callback_entered,10000)!=WAIT_OBJECT_0)
        fail("conversion callback has record reference");
    SetEvent(convert_begin);
    if(!bridge_convert(NULL)) fail("concurrent native fiber conversion");
    if(WaitForSingleObject(convert_freed,10000)!=WAIT_OBJECT_0)
        fail("concurrent FlsFree completion");
    if(bridge_get(convert_guard_slot)!=&rundown_b_value)
        fail("conversion lost a different slot's value");
    return 0;
}
static DWORD WINAPI concurrent_conversion_free(LPVOID unused)
{
    (void)unused;
    if(!bridge_free(convert_slot)) fail("concurrent conversion FlsFree");
    SetEvent(convert_freed); return 0;
}
static void concurrent_conversion_regression(void)
{
    DWORD round,id; HANDLE threads[2];
    convert_ready=CreateEventA(NULL,TRUE,FALSE,NULL);
    convert_callback_entered=CreateEventA(NULL,TRUE,FALSE,NULL);
    convert_begin=CreateEventA(NULL,TRUE,FALSE,NULL);
    convert_freed=CreateEventA(NULL,TRUE,FALSE,NULL);
    if(!convert_ready||!convert_callback_entered||!convert_begin||!convert_freed)
        fail("conversion contention events");
    for(round=0;round<32;++round) {
        ResetEvent(convert_ready); ResetEvent(convert_callback_entered);
        ResetEvent(convert_begin); ResetEvent(convert_freed);
        convert_slot=bridge_alloc(concurrent_conversion_callback);
        convert_guard_slot=bridge_alloc(NULL);
        if(convert_slot==FLS_OUT_OF_INDEXES||convert_guard_slot==FLS_OUT_OF_INDEXES)
            fail("conversion contention slots");
        threads[0]=bridge_create_thread(NULL,0,concurrent_converter,NULL,0,&id);
        if(!threads[0]||WaitForSingleObject(convert_ready,10000)!=WAIT_OBJECT_0)
            fail("concurrent converter ready");
        threads[1]=bridge_create_thread(NULL,0,concurrent_conversion_free,NULL,0,&id);
        if(!threads[1]||WaitForMultipleObjects(2,threads,TRUE,10000)!=WAIT_OBJECT_0)
            fail("conversion/free contention joins");
        CloseHandle(threads[0]); CloseHandle(threads[1]);
        if(!bridge_free(convert_guard_slot)) fail("conversion contention guard free");
    }
    CloseHandle(convert_ready); CloseHandle(convert_callback_entered);
    CloseHandle(convert_begin); CloseHandle(convert_freed);
    say("PASS: FLS 32 concurrent conversion/free transfers preserve live values\r\n");
}
static BOOL command_contains(const char *needle)
{
    const char *text=GetCommandLineA();
    for(;*text;++text) { const char *a=text,*b=needle; while(*a&&*a==*b){++a;++b;} if(!*b)return TRUE; }
    return FALSE;
}

static DWORD term_a,term_b,term_guard,term_mode,term_hold,term_rundown;
static HANDLE term_foreign_ready,term_foreign_release;
static HANDLE term_owner_ready,term_hold_entered,term_hold_release;
static int term_own_value,term_foreign_value;
static volatile LONG term_a_own,term_a_foreign,term_b_own,term_b_foreign,term_guard_calls;
static VOID WINAPI term_a_callback(PVOID value)
{
    if(value==&term_foreign_value) {
        InterlockedIncrement(&term_a_foreign);
        if(term_mode==4&&term_a_own) bridge_exit(74); /* exit again during abandonment */
        return;
    }
    if(value!=&term_own_value) fail("termination A value");
    InterlockedIncrement(&term_a_own);
    if(term_mode==0) return;
    if(term_mode==2) bridge_free_exit(fixture,72);
    else if(term_mode==3) bridge_delete(GetCurrentFiber());
    else bridge_exit(71);
    fail("callback thread termination returned");
}
static VOID WINAPI term_b_callback(PVOID value)
{
    if(value==&term_foreign_value) { InterlockedIncrement(&term_b_foreign); return; }
    if(value!=&term_own_value) fail("termination B value");
    InterlockedIncrement(&term_b_own);
    if(!bridge_free(term_a)) fail("nested retirement A");
}
static VOID WINAPI term_guard_callback(PVOID value)
{
    if(value!=&term_own_value) fail("termination guard value");
    InterlockedIncrement(&term_guard_calls);
}
static VOID WINAPI term_hold_callback(PVOID value)
{
    if(value!=&term_own_value) fail("termination foreign in-flight value");
    SetEvent(term_hold_entered);
    if(WaitForSingleObject(term_hold_release,5000)!=WAIT_OBJECT_0)
        fail("termination foreign in-flight release");
}
static DWORD WINAPI term_hold_worker(LPVOID unused)
{
    (void)unused;
    if(!bridge_free(term_hold)) fail("termination foreign retirement");
    return 0;
}
static VOID WINAPI term_rundown_callback(PVOID value)
{
    if(value!=&term_own_value||!bridge_free(term_b)) fail("nested retirement during rundown");
    fail("terminating nested rundown callback returned");
}
static DWORD WINAPI term_foreign_worker(LPVOID unused)
{
    (void)unused;
    if(!bridge_set(term_a,&term_foreign_value)||!bridge_set(term_b,&term_foreign_value))
        fail("termination foreign values");
    SetEvent(term_foreign_ready);
    if(WaitForSingleObject(term_foreign_release,10000)!=WAIT_OBJECT_0)
        fail("termination foreign release");
    return 0;
}
static DWORD WINAPI term_owner_worker(LPVOID unused)
{
    (void)unused;
    if(term_mode==3&&!bridge_convert(NULL)) fail("termination convert");
    if(!bridge_set(term_a,&term_own_value)||!bridge_set(term_b,&term_own_value)||
       !bridge_set(term_guard,&term_own_value)) fail("termination owner values");
    if(term_mode==5) {
        if(!bridge_set(term_hold,&term_own_value)) fail("termination held value");
        SetEvent(term_owner_ready);
        if(WaitForSingleObject(term_hold_entered,5000)!=WAIT_OBJECT_0)
            fail("termination foreign callback entered");
    }
    if(term_mode==6) {
        if(!bridge_set(term_rundown,&term_own_value)) fail("termination rundown value");
        return 0;
    }
    if(term_mode==2&&!LoadLibraryA("FLSFIX.DLL")) fail("termination fixture lifetime pin");
    if(!bridge_free(term_b)) fail("outer retirement B");
    if(term_mode) fail("terminating callback returned to FlsFree caller");
    return 70;
}
static void termination_child(BOOL native)
{
    HANDLE foreign,owner,holder=NULL; DWORD id,code,reused_a,reused_b;
    if(native) {
        HMODULE kernel=GetModuleHandleA("KERNEL32.DLL");
        bridge_alloc=(alloc_fn)GetProcAddress(kernel,"FlsAlloc");
        bridge_free=(free_fn)GetProcAddress(kernel,"FlsFree");
        bridge_get=(get_fn)GetProcAddress(kernel,"FlsGetValue");
        bridge_set=(set_fn)GetProcAddress(kernel,"FlsSetValue");
        if(!bridge_alloc||!bridge_free||!bridge_get||!bridge_set) ExitProcess(77);
        bridge_create_thread=CreateThread; bridge_exit=ExitThread;
        bridge_free_exit=FreeLibraryAndExitThread;
        bridge_convert=ConvertThreadToFiber; bridge_delete=DeleteFiber;
    }
    if(command_contains("-exit")) term_mode=1;
    if(command_contains("-unload")) term_mode=2;
    if(command_contains("-delete")) term_mode=3;
    if(command_contains("-repeat")) term_mode=4;
    if(command_contains("-foreign")) term_mode=5;
    if(command_contains("-rundown")) term_mode=6;
    term_foreign_ready=CreateEventA(NULL,TRUE,FALSE,NULL);
    term_foreign_release=CreateEventA(NULL,TRUE,FALSE,NULL);
    if(!term_foreign_ready||!term_foreign_release) fail("termination events");
    if(term_mode==6) {
        term_rundown=bridge_alloc(term_rundown_callback);
        if(term_rundown==FLS_OUT_OF_INDEXES) fail("termination rundown slot");
    }
    term_a=bridge_alloc(term_a_callback); term_b=bridge_alloc(term_b_callback);
    term_guard=bridge_alloc(term_guard_callback);
    if(term_a==FLS_OUT_OF_INDEXES||term_b==FLS_OUT_OF_INDEXES||term_guard==FLS_OUT_OF_INDEXES)
        fail("termination slots");
    if(term_mode==5) {
        term_hold=bridge_alloc(term_hold_callback);
        term_owner_ready=CreateEventA(NULL,TRUE,FALSE,NULL);
        term_hold_entered=CreateEventA(NULL,TRUE,FALSE,NULL);
        term_hold_release=CreateEventA(NULL,TRUE,FALSE,NULL);
        if(term_hold==FLS_OUT_OF_INDEXES||!term_owner_ready||!term_hold_entered||!term_hold_release)
            fail("termination in-flight setup");
    }
    foreign=bridge_create_thread(NULL,0,term_foreign_worker,NULL,0,&id);
    if(!foreign||WaitForSingleObject(term_foreign_ready,5000)!=WAIT_OBJECT_0)
        fail("termination foreign ready");
    owner=bridge_create_thread(NULL,0,term_owner_worker,NULL,0,&id);
    if(term_mode==5) {
        if(!owner||WaitForSingleObject(term_owner_ready,5000)!=WAIT_OBJECT_0)
            fail("termination owner holds foreign value");
        holder=bridge_create_thread(NULL,0,term_hold_worker,NULL,0,&id);
        if(!holder||WaitForSingleObject(term_hold_entered,5000)!=WAIT_OBJECT_0)
            fail("termination foreign callback active");
        if(WaitForSingleObject(owner,100)!=WAIT_TIMEOUT) {
            if(!native) fail("termination ignored foreign reference");
            say("OBSERVED: native owner terminated before foreign callback release\r\n");
        }
        SetEvent(term_hold_release);
    }
    if(!owner||WaitForSingleObject(owner,2000)!=WAIT_OBJECT_0) {
        if(native) {
            say("OBSERVED: native nested callback termination did not complete within 2 seconds\r\n");
            ExitProcess(124);
        }
        fail("callback termination deadlocked its thread");
    }
    if(!GetExitCodeThread(owner,&code)) fail("termination exit code");
    if(!native&&term_mode!=3&&code!=(term_mode==0?70:term_mode==2?72:term_mode==4?74:71))
        fail("termination exit status preserved");
    if(term_a_own!=1||term_a_foreign!=1||term_b_own!=1||term_b_foreign!=1||term_guard_calls!=1)
        fail("abandoned retirement omitted or repeated callbacks");
    reused_a=bridge_alloc(NULL); reused_b=bridge_alloc(NULL);
    if(reused_a==FLS_OUT_OF_INDEXES||reused_b==FLS_OUT_OF_INDEXES)
        fail("post-termination allocation");
    if(!native&& !((reused_a==term_a&&reused_b==term_b)||(reused_a==term_b&&reused_b==term_a)))
        fail("abandoned retiring slots not reusable");
    if(!bridge_set(reused_a,&term_own_value)||bridge_get(reused_a)!=&term_own_value||
       !bridge_free(reused_a)||!bridge_free(reused_b)||!bridge_free(term_guard))
        fail("post-termination generation reuse");
    if(term_mode==6&&!bridge_free(term_rundown)) fail("termination rundown slot free");
    if(holder) {
        if(WaitForSingleObject(holder,5000)!=WAIT_OBJECT_0) fail("foreign retirement completed");
        CloseHandle(holder); CloseHandle(term_owner_ready);
        CloseHandle(term_hold_entered); CloseHandle(term_hold_release);
    }
    SetEvent(term_foreign_release);
    if(WaitForSingleObject(foreign,5000)!=WAIT_OBJECT_0) fail("termination foreign completion");
    if(term_a_foreign!=1||term_b_foreign!=1) fail("old foreign generation survived reuse");
    CloseHandle(owner); CloseHandle(foreign);
    CloseHandle(term_foreign_ready); CloseHandle(term_foreign_release);
    say(native?"OBSERVED: native bounded callback termination completed\r\n":
               "PASS: bridge bounded callback termination and slot reuse\r\n");
    ExitProcess(0);
}
static void termination_parent(BOOL native)
{
    static const char *modes[]={"-normal","-exit","-unload","-delete","-repeat","-foreign","-rundown"};
    DWORD i,n,j,code; char path[MAX_PATH],command[MAX_PATH+96];
    STARTUPINFOA startup={0}; PROCESS_INFORMATION process;
    BOOL failed=FALSE;
    if(!GetModuleFileNameA(NULL,path,sizeof(path))) fail("termination child path");
    for(i=0;i<7;++i) {
        n=0; command[n++]='"'; for(j=0;path[j];++j)command[n++]=path[j];command[n++]='"';
        { const char *s=native?" /term-child-native":" /term-child-bridge"; while(*s)command[n++]=*s++; }
        for(j=0;modes[i][j];++j)command[n++]=modes[i][j]; command[n]=0;
        startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
        if(!CreateProcessA(NULL,command,NULL,NULL,TRUE,0,NULL,NULL,&startup,&process))
            fail("termination child launch");
        if(WaitForSingleObject(process.hProcess,8000)!=WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess,124); WaitForSingleObject(process.hProcess,2000);
            code=124;
        } else if(!GetExitCodeProcess(process.hProcess,&code)) fail("termination child status");
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        say(native?"OBSERVED native ":"CHECK bridge "); say(modes[i]); hex32(" exit=",code);
        if(!native&&code) failed=TRUE;
    }
    if(failed) fail("bounded callback termination child");
    if(!native) say("PASS: FLS seven bounded nested-retirement termination children\r\n");
}

static DWORD self_slot, self_other;
static LONG self_calls;
static VOID WINAPI self_free_callback(PVOID value)
{
    if(value!=&marker_natural) fail("self-free callback value");
    InterlockedIncrement(&self_calls);
    if(!bridge_free(self_slot)) fail("rundown callback frees its own slot");
    self_other=bridge_alloc(NULL);
    if(self_other==FLS_OUT_OF_INDEXES||self_other==self_slot)
        fail("active self-free callback generation reused");
}
static DWORD WINAPI self_free_worker(LPVOID unused)
{
    (void)unused;
    if(!bridge_set(self_slot,&marker_natural)) fail("self-free worker value");
    return 0;
}
static void self_free_rundown_regression(void)
{
    DWORD id, again;
    HANDLE worker;
    self_slot=bridge_alloc(self_free_callback);
    if(self_slot==FLS_OUT_OF_INDEXES) fail("self-free allocation");
    worker=bridge_create_thread(NULL,0,self_free_worker,NULL,0,&id);
    if(!worker||WaitForSingleObject(worker,5000)!=WAIT_OBJECT_0||self_calls!=1)
        fail("self-free callback deadlocked or repeated");
    CloseHandle(worker);
    again=bridge_alloc(NULL);
    if(again!=self_slot) fail("self-free completed generation not reusable");
    if(!bridge_free(again)||!bridge_free(self_other)) fail("self-free cleanup");
    say("PASS: FLS rundown callback self-free keeps generation until return\r\n");
}

void mainCRTStartup(void)
{
    table_fn get_table;
    const m98_api_table *table;
    LPVOID child, later;
    DWORD old_slot;
    fixture = LoadLibraryA("FLSFIX.DLL");
    if (!fixture) fail("LoadLibrary FLSFIX.DLL");
    get_table = (table_fn)GetProcAddress(fixture, "get_api_table");
    if (!get_table) fail("get_api_table");
    table = get_table();
    if (!table || !equal(table->target_library, "KERNEL32.DLL"))
        fail("fixture API table");
    bridge_alloc = (alloc_fn)api(table, "FlsAlloc");
    bridge_free = (free_fn)api(table, "FlsFree");
    bridge_get = (get_fn)api(table, "FlsGetValue");
    bridge_set = (set_fn)api(table, "FlsSetValue");
    bridge_create_fiber = (fiber_create_fn)api(table, "CreateFiber");
    bridge_convert = (fiber_convert_fn)api(table, "ConvertThreadToFiber");
    bridge_switch = (fiber_switch_fn)api(table, "SwitchToFiber");
    bridge_delete = (fiber_delete_fn)api(table, "DeleteFiber");
    bridge_create_thread = (thread_create_fn)api(table, "CreateThread");
    bridge_exit = (thread_exit_fn)api(table, "ExitThread");
    bridge_free_exit = (free_exit_fn)api(table, "FreeLibraryAndExitThread");

    if(command_contains("/term-child-")) termination_child(command_contains("/term-child-native"));
    if(command_contains("/term-native")) { termination_parent(TRUE); ExitProcess(0); }
    if(command_contains("/term-bridge")) { termination_parent(FALSE); ExitProcess(0); }
    if(command_contains("/review-self-free")) { self_free_rundown_regression(); ExitProcess(0); }

    /* Each mode can demonstrate its pre-fix failure without reaching another
     * regression first. All modes are also exercised in the normal suite. */
    if(command_contains("/review-slot")) { rundown_slot_regression(); ExitProcess(0); }
    if(command_contains("/review-convert")) { conversion_reentry_regression(); ExitProcess(0); }
    if(command_contains("/review-delete")) { current_delete_regression(); ExitProcess(0); }
    if(command_contains("/review-transfer")) { concurrent_conversion_regression(); ExitProcess(0); }

    native_host_comparison("before fiber conversion");

    slot = bridge_alloc(callback);
    if (slot == FLS_OUT_OF_INDEXES || !slot) fail("FlsAlloc");
    SetLastError(123);
    if (bridge_get(slot) || GetLastError() != ERROR_SUCCESS)
        fail("valid empty FlsGetValue");
    SetLastError(0);
    if (bridge_get(0) || GetLastError() != ERROR_INVALID_PARAMETER)
        fail("FLS index zero validation");
    if (!bridge_set(slot, &marker_main) ||
        bridge_get(slot) != &marker_main) fail("thread FLS value");
    main_fiber = bridge_convert(&marker_main);
    if (!main_fiber || GetCurrentFiber() != main_fiber ||
        bridge_get(slot) != &marker_main) fail("ConvertThreadToFiber value move");
    child = bridge_create_fiber(0, child_fiber, &marker_parameter);
    if (!child) fail("CreateFiber child");
    bridge_switch(child);
    if (bridge_get(slot) != &marker_main) fail("fiber values were shared");
    test_reentry = TRUE;
    if (!bridge_free(slot)) fail("FlsFree across two fibers");
    test_reentry = FALSE;
    if (callback_count != 2 || reentry_errors != 4)
        fail("FlsFree callback count or reentry");
    bridge_delete(child);
    if (callback_count != 2) fail("deleted already-cleared child callback");
    if (temporary_slot == FLS_OUT_OF_INDEXES ||
        !bridge_free(temporary_slot)) fail("free temporary callback slot");
    old_slot = slot;
    slot = bridge_alloc(callback);
    if (slot != old_slot) fail("FLS index reuse");
    SetLastError(123);
    if (bridge_get(slot) || GetLastError() != ERROR_SUCCESS)
        fail("reused FLS index inherited old value");

    live_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    live_release = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!live_ready || !live_release) fail("live thread events");
    {
        HANDLE live = bridge_create_thread(NULL, 0, live_worker,
                                           &marker_live, 0, NULL);
        DWORD code = 0;
        if (!live || WaitForSingleObject(live_ready, 10000) != WAIT_OBJECT_0)
            fail("live worker ready");
        if (!bridge_set(slot, &marker_main) || !bridge_free(slot) ||
            callback_count != 4) fail("FlsFree across live thread and fiber");
        if (!SetEvent(live_release) ||
            WaitForSingleObject(live, 10000) != WAIT_OBJECT_0 ||
            !GetExitCodeThread(live, &code) || code != 29)
            fail("live worker after FlsFree");
        CloseHandle(live);
    }
    CloseHandle(live_ready);
    CloseHandle(live_release);
    slot = bridge_alloc(callback);
    if (slot != old_slot) fail("reuse after live-thread FlsFree");
    SetLastError(123);
    if (bridge_get(slot) || GetLastError() != ERROR_SUCCESS)
        fail("live-thread freed slot retained a value");

    later = bridge_create_fiber(0, deletion_fiber, &marker_parameter);
    if (!later) fail("CreateFiber deletion callback");
    bridge_switch(later);
    bridge_delete(later);
    if (callback_count != 5) fail("DeleteFiber callback");
    if (!bridge_set(slot, &marker_main)) fail("main value after slot reuse");
    join_worker(natural_worker, &marker_natural, 17);
    if (callback_count != 6) fail("natural thread return callback");
    join_worker(explicit_worker, &marker_explicit, 19);
    if (callback_count != 7) fail("ExitThread callback");
    if (!LoadLibraryA("FLSFIX.DLL")) fail("pin fixture for free-exit worker");
    join_worker(unload_worker, &marker_free_exit, 23);
    if (callback_count != 8) fail("FreeLibraryAndExitThread callback");
    if (!bridge_free(slot) || callback_count != 9)
        fail("main fiber callback after thread exits");
    native_host_comparison("after fiber conversion");
    rundown_slot_regression();
    conversion_reentry_regression();
    current_delete_regression();
    concurrent_conversion_regression();
    self_free_rundown_regression();
    termination_parent(FALSE);
    say("PASS: direct FLS fixture slots, fibers, callbacks, thread exit hooks\r\n");
    ExitProcess(0);
}
