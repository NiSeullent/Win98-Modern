/* Callback-family contracts shared by native Windows and Win98 fixtures. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include "../src/kex_abi.h"

typedef struct callback_ops {
    PTP_WORK (WINAPI *create)(PTP_WORK_CALLBACK,PVOID,PTP_CALLBACK_ENVIRON);
    void (WINAPI *submit)(PTP_WORK);
    void (WINAPI *wait)(PTP_WORK,BOOL);
    void (WINAPI *close)(PTP_WORK);
    void (WINAPI *free_library)(PTP_CALLBACK_INSTANCE,HMODULE);
    void (WINAPI *leave_section)(PTP_CALLBACK_INSTANCE,PCRITICAL_SECTION);
    void (WINAPI *release_mutex)(PTP_CALLBACK_INSTANCE,HANDLE);
    void (WINAPI *release_semaphore)(PTP_CALLBACK_INSTANCE,HANDLE,DWORD);
    void (WINAPI *set_event)(PTP_CALLBACK_INSTANCE,HANDLE);
    void (WINAPI *disassociate)(PTP_CALLBACK_INSTANCE);
    BOOL (WINAPI *run_long)(PTP_CALLBACK_INSTANCE);
    BOOL (WINAPI *simple)(PTP_SIMPLE_CALLBACK,PVOID,PTP_CALLBACK_ENVIRON);
} callback_ops;
static callback_ops ops;

typedef struct callback_state {
    HANDLE entered, release, done, mutex, semaphore;
    CRITICAL_SECTION section;
    volatile LONG completed, finalizations, long_count;
    HMODULE library;
    HANDLE unload_event;
} callback_state;
typedef void (*marker_set_unload_event_fn)(HANDLE);

static void say(const char *text)
{
    DWORD length=0,written;
    while(text[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,length,&written,NULL);
}

static void fail(const char *text)
{
    say("FAIL: ");say(text);say("\r\n");ExitProcess(1);
}

static void await(HANDLE event,const char *message)
{
    if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0) fail(message);
}

static void open_state(callback_state *s)
{
    s->entered=CreateEventA(NULL,TRUE,FALSE,NULL);
    s->release=CreateEventA(NULL,TRUE,FALSE,NULL);
    s->done=CreateEventA(NULL,TRUE,FALSE,NULL);
    s->mutex=CreateMutexA(NULL,FALSE,NULL);
    s->semaphore=CreateSemaphoreA(NULL,0,4,NULL);
    s->completed=s->finalizations=s->long_count=0;
    s->library=NULL;
    s->unload_event=NULL;
    InitializeCriticalSection(&s->section);
    if(!s->entered||!s->release||!s->done||!s->mutex||!s->semaphore) fail("state allocation");
}

static void close_state(callback_state *s)
{
    CloseHandle(s->entered);CloseHandle(s->release);CloseHandle(s->done);
    CloseHandle(s->mutex);CloseHandle(s->semaphore);DeleteCriticalSection(&s->section);
    if(s->unload_event)CloseHandle(s->unload_event);
}

static void prepare_marker_unload(callback_state *s)
{
    marker_set_unload_event_fn set_unload_event;
    s->library=LoadLibraryA("TPMARK.DLL");
    if(!s->library)fail("load deferred marker");
    s->unload_event=CreateEventA(NULL,TRUE,FALSE,NULL);
    set_unload_event=(marker_set_unload_event_fn)(ULONG_PTR)
        GetProcAddress(s->library,"marker_set_unload_event");
    if(!s->unload_event||!set_unload_event)fail("marker unload notification setup");
    set_unload_event(s->unload_event);
}

static void await_marker_unloaded(callback_state *s)
{
    DWORD start;
    await(s->unload_event,"deferred DLL detach notification");
    /* DllMain signals just before returning. A later module query can still
     * race the loader's final removal, so test that as a separate bounded
     * observation rather than assuming work wait is an unload fence. */
    start=GetTickCount();
    while(GetModuleHandleA("TPMARK.DLL")){
        if(GetTickCount()-start>=5000)fail("marker still loaded after DLL detach");
        Sleep(1);
    }
}

static void CALLBACK resource_callback(PTP_CALLBACK_INSTANCE instance,PVOID context,PTP_WORK work)
{
    callback_state *s=context;
    (void)work;
    EnterCriticalSection(&s->section);
    await(s->mutex,"callback acquire mutex");
    ops.leave_section(instance,&s->section);
    ops.release_mutex(instance,s->mutex);
    ops.release_semaphore(instance,s->semaphore,2);
    ops.set_event(instance,s->done);
    ops.free_library(instance,s->library);
    SetEvent(s->entered);
    await(s->release,"resource callback release gate");
    InterlockedIncrement(&s->completed);
}

static void CALLBACK disassociate_callback(PTP_CALLBACK_INSTANCE instance,PVOID context,PTP_WORK work)
{
    callback_state *s=context;
    (void)work;
    ops.set_event(instance,s->done);
    ops.disassociate(instance);
    SetEvent(s->entered);
    await(s->release,"disassociated callback release gate");
    InterlockedIncrement(&s->completed);
}

typedef struct wait_context {PTP_WORK work;HANDLE done;} wait_context;
static DWORD WINAPI wait_worker(PVOID context)
{
    wait_context *w=context;
    ops.wait(w->work,FALSE);
    SetEvent(w->done);
    return 0;
}

static void CALLBACK simple_callback(PTP_CALLBACK_INSTANCE instance,PVOID context)
{
    callback_state *s=context;
    InterlockedIncrement(&s->completed);
    ops.set_event(instance,s->done);
}

static void CALLBACK finalization_callback(PTP_CALLBACK_INSTANCE instance,PVOID context)
{
    callback_state *s=context;
    (void)instance;
    InterlockedIncrement(&s->finalizations);
}

static void CALLBACK long_callback(PTP_CALLBACK_INSTANCE instance,PVOID context,PTP_WORK work)
{
    callback_state *s=context;
    (void)work;
    if(!ops.run_long(instance)) fail("CallbackMayRunLong available/grown worker");
    if(InterlockedIncrement(&s->long_count)==4) SetEvent(s->entered);
    await(s->release,"long callbacks release gate");
    InterlockedIncrement(&s->completed);
}

#ifndef M98_STATIC
static BOOL same(const char *a,const char *b)
{
    while(*a&&*a==*b){++a;++b;}return *a==*b;
}
static void *lookup(const m98_api_table *table,const char *name)
{
    int n;
    for(n=0;n<table->named_apis_count;++n)
        if(same(table->named_apis[n].name,name))return (void *)(ULONG_PTR)table->named_apis[n].addr;
    fail(name);return NULL;
}
#define LOAD(field,name) ops.field=(void *)lookup(table,#name)
#else
#define LOAD(field,name) ops.field=name
#endif

void mainCRTStartup(void)
{
    callback_state resources, detached, simple, longer, child;
    PTP_WORK work;
    TP_CALLBACK_ENVIRON env;
    wait_context waiter;
    HANDLE thread;
    DWORD thread_id;
    int n;
#ifndef M98_STATIC
    typedef const m98_api_table *(*table_fn)(void);
#ifdef M98_INTEGRATED
    HMODULE module=LoadLibraryA("m98wrap.dll");
#else
    HMODULE module=LoadLibraryA("threadpool_fixture.dll");
#endif
    table_fn get_table;
    const m98_api_table *table;
    if(!module)fail("load callback provider");
    get_table=(table_fn)GetProcAddress(module,"get_api_table");
    if(!get_table)fail("callback API table");
    table=get_table();
#endif
    LOAD(create,CreateThreadpoolWork);LOAD(submit,SubmitThreadpoolWork);
    LOAD(wait,WaitForThreadpoolWorkCallbacks);LOAD(close,CloseThreadpoolWork);
    LOAD(free_library,FreeLibraryWhenCallbackReturns);
    LOAD(leave_section,LeaveCriticalSectionWhenCallbackReturns);
    LOAD(release_mutex,ReleaseMutexWhenCallbackReturns);
    LOAD(release_semaphore,ReleaseSemaphoreWhenCallbackReturns);
    LOAD(set_event,SetEventWhenCallbackReturns);
    LOAD(disassociate,DisassociateCurrentThreadFromCallback);
    LOAD(run_long,CallbackMayRunLong);LOAD(simple,TrySubmitThreadpoolCallback);

    open_state(&resources);
    prepare_marker_unload(&resources);
    work=ops.create(resource_callback,&resources,NULL);
    if(!work)fail("resource work allocation");
    ops.submit(work);await(resources.entered,"resource callback entered");
    if(WaitForSingleObject(resources.done,0)!=WAIT_TIMEOUT||
       WaitForSingleObject(resources.unload_event,0)!=WAIT_TIMEOUT||
       WaitForSingleObject(resources.semaphore,0)!=WAIT_TIMEOUT||
       WaitForSingleObject(resources.mutex,0)!=WAIT_TIMEOUT||
       TryEnterCriticalSection(&resources.section))fail("resources released before callback return");
    if(!GetModuleHandleA("TPMARK.DLL"))fail("marker unloaded before callback return");
    SetEvent(resources.release);ops.wait(work,FALSE);
    await_marker_unloaded(&resources);
    if(resources.completed!=1||WaitForSingleObject(resources.done,0)!=WAIT_OBJECT_0)
        fail("resource callback completion");
    if(WaitForSingleObject(resources.semaphore,0)!=WAIT_OBJECT_0||
       WaitForSingleObject(resources.semaphore,0)!=WAIT_OBJECT_0||
       WaitForSingleObject(resources.semaphore,0)!=WAIT_TIMEOUT)fail("deferred semaphore count");
    if(WaitForSingleObject(resources.mutex,0)!=WAIT_OBJECT_0)fail("deferred mutex ownership");
    ReleaseMutex(resources.mutex);
    if(!TryEnterCriticalSection(&resources.section))fail("deferred critical section");
    LeaveCriticalSection(&resources.section);
    ops.close(work);close_state(&resources);

    open_state(&detached);
    work=ops.create(disassociate_callback,&detached,NULL);
    if(!work)fail("disassociate work allocation");
    ops.submit(work);await(detached.entered,"callback disassociated");
    waiter.work=work;waiter.done=CreateEventA(NULL,TRUE,FALSE,NULL);
    thread=CreateThread(NULL,0,wait_worker,&waiter,0,&thread_id);
    if(!thread)fail("disassociate waiter thread");
    await(waiter.done,"disassociation must release callback waiters");
    if(detached.completed||WaitForSingleObject(detached.done,0)!=WAIT_TIMEOUT)
        fail("disassociation ran return cleanup early");
    ops.close(work);
    SetEvent(detached.release);await(detached.done,"closed/disassociated callback return");
    if(detached.completed!=1)fail("closed callback lifetime");
    await(thread,"waiter exit");CloseHandle(thread);CloseHandle(waiter.done);close_state(&detached);

    open_state(&simple);
    InitializeThreadpoolEnvironment(&env);
    env.FinalizationCallback=finalization_callback;
    if(!ops.simple(simple_callback,&simple,&env))fail("simple callback submission");
    await(simple.done,"simple callback return cleanup");
    if(simple.completed!=1||simple.finalizations!=1)fail("simple context/finalization");
    close_state(&simple);

    open_state(&longer);open_state(&child);
    work=ops.create(long_callback,&longer,NULL);
    if(!work)fail("long work allocation");
    for(n=0;n<4;++n)ops.submit(work);
    await(longer.entered,"four long callbacks executing");
    if(!ops.simple(simple_callback,&child,NULL))fail("child submission under saturated pool");
    await(child.done,"long callback capacity must allow child work");
    SetEvent(longer.release);ops.wait(work,FALSE);
    if(longer.completed!=4||child.completed!=1)fail("long callback counts");
    ops.close(work);close_state(&longer);close_state(&child);
#ifdef M98_STATIC
    say("PASS: static callback deferred resources, disassociation, simple work and long-work capacity\r\n");
#else
    say("PASS: callback deferred resources, disassociation, simple work and long-work capacity\r\n");
#endif
    ExitProcess(0);
}
