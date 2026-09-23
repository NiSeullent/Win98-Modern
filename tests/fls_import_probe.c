/* Whole FLS/lifecycle binding probe; static KERNEL32 or explicit API table.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"
#include "../src/m98_fls.h"

#ifdef M98_FLS_STATIC
__declspec(dllimport) DWORD WINAPI FlsAlloc(M98_FLS_CALLBACK);
__declspec(dllimport) BOOL WINAPI FlsFree(DWORD);
__declspec(dllimport) PVOID WINAPI FlsGetValue(DWORD);
__declspec(dllimport) BOOL WINAPI FlsSetValue(DWORD,PVOID);
__declspec(dllimport) LPVOID WINAPI CreateFiberEx(SIZE_T,SIZE_T,DWORD,LPFIBER_START_ROUTINE,LPVOID);
__declspec(dllimport) LPVOID WINAPI ConvertThreadToFiberEx(LPVOID,DWORD);
#endif

static struct {
    __typeof__(&m98_FlsAlloc) alloc;
    __typeof__(&m98_FlsFree) free;
    __typeof__(&m98_FlsGetValue) get;
    __typeof__(&m98_FlsSetValue) set;
    __typeof__(&m98_CreateThread) thread;
    __typeof__(&m98_ExitThread) exit;
    __typeof__(&m98_FreeLibraryAndExitThread) free_exit;
    __typeof__(&m98_CreateFiber) fiber;
    __typeof__(&m98_CreateFiberEx) fiber_ex;
    __typeof__(&m98_ConvertThreadToFiber) convert;
    __typeof__(&m98_ConvertThreadToFiberEx) convert_ex;
    __typeof__(&m98_SwitchToFiber) switch_to;
    __typeof__(&m98_DeleteFiber) delete;
} api;
static DWORD slot;
static int marks[7]={0,1,2,3,4,5,6};
static volatile LONG callbacks[7];
static LPVOID primary;
static HMODULE marker;
static volatile LONG module_visible_at_callback=-1;

static void say(const char *s)
{
    DWORD n=0,w;
    while(s[n])++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,n,&w,NULL);
}
static void fail(const char *s)
{
    say("FAIL: ");say(s);say("\r\n");ExitProcess(1);
}
#ifndef M98_FLS_STATIC
static BOOL equal(const char *a,const char *b)
{
    while(*a&&*a==*b){++a;++b;}return *a==*b;
}
static void *find(const m98_api_table *t,const char *name)
{
    int i;
    for(i=0;i<t->named_apis_count;++i)
        if(equal(t->named_apis[i].name,name))return (void *)t->named_apis[i].addr;
    fail(name);return NULL;
}
#endif
static void bind(void)
{
#ifdef M98_FLS_STATIC
    api.alloc=FlsAlloc;api.free=FlsFree;api.get=FlsGetValue;api.set=FlsSetValue;
    api.thread=CreateThread;api.exit=ExitThread;api.free_exit=FreeLibraryAndExitThread;
    api.fiber=CreateFiber;api.fiber_ex=CreateFiberEx;api.convert=ConvertThreadToFiber;
    api.convert_ex=ConvertThreadToFiberEx;api.switch_to=SwitchToFiber;api.delete=DeleteFiber;
#else
    typedef const m98_api_table *(*table_fn)(void);
    HMODULE module=LoadLibraryA(
#ifdef M98_FLS_INTEGRATED
        "M98WRAP.DLL"
#else
        "FLSFIX.DLL"
#endif
    );
    table_fn table;
    const m98_api_table *t;
    if(!module)fail("load FLS API provider");
    table=(table_fn)GetProcAddress(module,"get_api_table");
    if(!table)fail("API table export");
    t=table();
    if(!t||!equal(t->target_library,"KERNEL32.DLL"))fail("KERNEL32 table");
#define BIND(field,name) api.field=(__typeof__(api.field))find(t,name)
    BIND(alloc,"FlsAlloc");BIND(free,"FlsFree");BIND(get,"FlsGetValue");BIND(set,"FlsSetValue");
    BIND(thread,"CreateThread");BIND(exit,"ExitThread");BIND(free_exit,"FreeLibraryAndExitThread");
    BIND(fiber,"CreateFiber");BIND(fiber_ex,"CreateFiberEx");BIND(convert,"ConvertThreadToFiber");
    BIND(convert_ex,"ConvertThreadToFiberEx");BIND(switch_to,"SwitchToFiber");BIND(delete,"DeleteFiber");
#undef BIND
#endif
}
static VOID WINAPI cleanup(PVOID value)
{
    int id=*(int *)value;
    if(id<0||id>6)fail("callback value identity");
    InterlockedIncrement(&callbacks[id]);
    if(id==5)module_visible_at_callback=GetModuleHandleA("TPMARK.DLL")!=NULL;
}
static VOID WINAPI fiber_body(PVOID value)
{
    if(GetFiberData()!=value||!api.set(slot,value)||api.get(slot)!=value)
        fail("fiber value/parameter");
    api.switch_to(primary);
    fail("deleted fiber resumed");
}
static DWORD WINAPI worker(PVOID value)
{
    int id=*(int *)value;
    if(id==6&&!api.convert_ex(value,0))fail("ConvertThreadToFiberEx");
    if(!api.set(slot,value)||api.get(slot)!=value)fail("thread value");
    if(id==4)api.exit(104);
    if(id==5)api.free_exit(marker,105);
    return 100+(DWORD)id;
}
static void join(int id)
{
    DWORD code;
    /* Explicit NULL tests the native Win98 thread-ID pointer boundary. */
    HANDLE thread=api.thread(NULL,0,worker,&marks[id],0,NULL);
    if(!thread)fail("CreateThread NULL thread-id output");
    if(WaitForSingleObject(thread,10000)!=WAIT_OBJECT_0)fail("thread lifecycle timeout");
    if(!GetExitCodeThread(thread,&code)||code!=100+(DWORD)id)fail("thread exit code");
    CloseHandle(thread);
    if(callbacks[id]!=1)fail("thread teardown callback count");
}
void mainCRTStartup(void)
{
    LPVOID child,extended;
    DWORD old;
    bind();
    slot=api.alloc(cleanup);
    if(slot==FLS_OUT_OF_INDEXES)fail("FlsAlloc");
    if(!api.set(slot,&marks[0])||api.get(slot)!=&marks[0])fail("unconverted-thread value");
    primary=api.convert(&marks[0]);
    if(!primary||GetCurrentFiber()!=primary||api.get(slot)!=&marks[0])fail("conversion value transfer");
    child=api.fiber(0,fiber_body,&marks[1]);
    extended=api.fiber_ex(0,0,0,fiber_body,&marks[2]);
    if(!child||!extended)fail("CreateFiber/CreateFiberEx");
    api.switch_to(child);api.switch_to(extended);
    if(api.get(slot)!=&marks[0])fail("fiber-local isolation");
    api.delete(child);
    if(callbacks[1]!=1||callbacks[0]||callbacks[2])fail("DeleteFiber isolated rundown");
    join(3);join(4);
    marker=LoadLibraryA("TPMARK.DLL");
    if(!marker)fail("load marker for FreeLibraryAndExitThread");
    join(5);
    if(GetModuleHandleA("TPMARK.DLL"))fail("FreeLibraryAndExitThread did not release module");
    say(module_visible_at_callback?"OBSERVED: module present during free-exit FLS callback\r\n":
                                   "OBSERVED: module absent during free-exit FLS callback\r\n");
    join(6);
    if(!api.free(slot)||callbacks[0]!=1||callbacks[2]!=1)fail("FlsFree across live fibers");
    api.delete(extended);
    if(callbacks[2]!=1)fail("double callback after freed-slot fiber delete");
    old=slot;slot=api.alloc(NULL);
    if(slot==FLS_OUT_OF_INDEXES||!api.set(slot,NULL)||api.get(slot)||GetLastError()!=ERROR_SUCCESS)
        fail("slot reuse/null roundtrip");
    (void)old; /* Native implementations need not reuse the same numeric slot. */
    if(!api.free(slot))fail("free reused slot");
#ifdef M98_FLS_STATIC
    say("PASS: static FLS and 13-API thread/fiber lifecycle contract subset\r\n");
#else
    say("PASS: table FLS and 13-API thread/fiber lifecycle contract subset\r\n");
#endif
    ExitProcess(0);
}
