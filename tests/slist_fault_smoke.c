/* Deterministic reclaimed-node SEH and real concurrent free stress.
 * Only this binary is built with M98_SLIST_TEST_HOOK. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/m98_slist.h"
static DECLSPEC_ALIGN(8) SLIST_HEADER head;
static DECLSPEC_ALIGN(8) SLIST_ENTRY spare;
static PSLIST_ENTRY page;
static HANDLE entered, changed;
static volatile LONG armed, stage, mode;
extern volatile LONG m98_slist_test_recovered;
static void say(const char *s) { DWORD n=0,w; while(s[n])++n; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,n,&w,0); }
static void require(BOOL b,const char *s) { if(!b){say("FAIL: ");say(s);say("\r\n");ExitProcess(1);} }

void __cdecl m98_slist_test_before_read(PSLIST_HEADER h, PSLIST_ENTRY entry)
{
    DWORD protection;
    if(h!=&head) return;
    if(InterlockedCompareExchange(&armed,0,1)==1) {
        stage=1; SetEvent(entered);
        require(WaitForSingleObject(changed,30000)==WAIT_OBJECT_0,"competitor changes head");
    } else if(mode==2&&stage==1&&entry==page) {
        /* Only the victim reaches this after the fault handler has retried;
         * competitor's own pop happens before it installs PAGE_NOACCESS. */
        if(m98_slist_test_recovered==2) {
            require(VirtualProtect(page,4096,PAGE_READWRITE,&protection),"restore reused node page");
            stage=2;
        }
    }
}
static DWORD WINAPI competitor(void *unused)
{
    PSLIST_ENTRY entry; DWORD protection;
    (void)unused;
    require(WaitForSingleObject(entered,30000)==WAIT_OBJECT_0,"victim captured old header");
    entry=m98_InterlockedPopEntrySList(&head);
    require(entry==page,"competitor removes captured node");
    if(mode==1) {
        require(VirtualFree(entry,0,MEM_RELEASE),"free captured node");
        m98_InterlockedPushEntrySList(&head,&spare);
    } else {
        m98_InterlockedPushEntrySList(&head,entry);
        require(VirtualProtect(entry,4096,PAGE_NOACCESS,&protection),"protect reused same address");
    }
    SetEvent(changed); return 0;
}
static void forced_race(LONG test_mode)
{
    HANDLE worker; PSLIST_ENTRY entry;
    mode=test_mode; stage=0;
    m98_InitializeSListHead(&head);
    page=VirtualAlloc(0,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    require(page!=0,"allocate race page");
    m98_InterlockedPushEntrySList(&head,page);
    ResetEvent(entered); ResetEvent(changed); armed=1;
    worker=CreateThread(0,0,competitor,0,0,0); require(worker!=0,"race worker");
    entry=m98_InterlockedPopEntrySList(&head);
    require(entry==(mode==1?&spare:page),"victim retries to current node");
    require(WaitForSingleObject(worker,30000)==WAIT_OBJECT_0,"race worker completes");
    CloseHandle(worker);
    require(m98_slist_test_recovered==mode,"one exact read exception recovered per forced race");
    require(!m98_InterlockedPopEntrySList(&head),"race list drained");
    if(mode==2) { require(stage==2,"same-pointer new-sequence retry"); VirtualFree(page,0,MEM_RELEASE); }
}

/* An unchanged invalid pointer must reach our caller's SEH handler. Redirect
 * to the wrapper's cleanup with the wrapper's saved stack, not past live
 * callee frames. This also proves Pop's handler does not eat unrelated faults. */
static volatile LONG propagated;
extern char slist_expected_fault_return[];
EXCEPTION_DISPOSITION __cdecl slist_outer_handler(EXCEPTION_RECORD *record,
    void *frame, CONTEXT *context, void *dispatcher)
{
    (void)dispatcher;
    if(record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||record->ExceptionFlags)
        return ExceptionContinueSearch;
    ++propagated;
    context->Eip=(DWORD)(ULONG_PTR)slist_expected_fault_return;
    context->Esp=(DWORD)(ULONG_PTR)frame;
    return ExceptionContinueExecution;
}
__attribute__((naked)) static void unchanged_invalid_pointer(void)
{
    __asm__ volatile(
        "pushl %ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "pushl $_slist_outer_handler\n\tpushl %fs:0\n\tmovl %esp,%fs:0\n\t"
        "pushl $_head\n\tcall _m98_InterlockedPopEntrySList@4\n\t"
        ".globl _slist_expected_fault_return\n_slist_expected_fault_return:\n\t"
        "movl (%esp),%eax\n\tmovl %eax,%fs:0\n\taddl $8,%esp\n\t"
        "popl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tretl\n\t");
}

static DECLSPEC_ALIGN(8) SLIST_HEADER free_race_head;
static DWORD WINAPI allocate_push_pop_free(void *unused)
{
    DWORD i; PSLIST_ENTRY entry;
    (void)unused;
    for(i=0;i<1500;++i) {
        entry=VirtualAlloc(0,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        require(entry!=0,"parallel page allocation");
        m98_InterlockedPushEntrySList(&free_race_head,entry);
        do { entry=m98_InterlockedPopEntrySList(&free_race_head); } while(!entry);
        require(VirtualFree(entry,0,MEM_RELEASE),"parallel reclaimed page release");
    }
    return 0;
}
static void concurrent_free(void)
{
    HANDLE workers[4]; DWORD i;
    m98_InitializeSListHead(&free_race_head); mode=0;
    for(i=0;i<4;++i) {
        workers[i]=CreateThread(0,0,allocate_push_pop_free,0,0,0);
        require(workers[i]!=0,"parallel free worker");
    }
    require(WaitForMultipleObjects(4,workers,TRUE,120000)==WAIT_OBJECT_0,"parallel free complete");
    for(i=0;i<4;++i) CloseHandle(workers[i]);
    require(!m98_InterlockedPopEntrySList(&free_race_head)&&!m98_QueryDepthSList(&free_race_head),"parallel free drained");
}
static void sequence_wrap(void)
{
    DWORD i;
    m98_InitializeSListHead(&head);
    for(i=0;i<32768;++i) {
        require(!m98_InterlockedPushEntrySList(&head,&spare),"sequence wrap push");
        require(m98_InterlockedPopEntrySList(&head)==&spare,"sequence wrap pop");
    }
    require(head.Sequence==0&&head.Depth==0&&!head.Next.Next,"sequence wraps independently after 65536 mutations");
    say("PASS: SList internal 16-bit sequence wrap after 65536 mutations\r\n");
}
void mainCRTStartup(void)
{
    DWORD protection;
    entered=CreateEventA(0,TRUE,FALSE,0); changed=CreateEventA(0,TRUE,FALSE,0);
    require(entered&&changed,"race events");
    forced_race(1); forced_race(2);
    say("PASS: SList reclaimed-page retry and same-pointer sequence retry\r\n");
    mode=0; m98_InitializeSListHead(&head);
    page=VirtualAlloc(0,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    require(page!=0,"invalid pointer page"); m98_InterlockedPushEntrySList(&head,page);
    require(VirtualProtect(page,4096,PAGE_NOACCESS,&protection),"invalid pointer protection");
    unchanged_invalid_pointer();
    require(propagated==1&&m98_slist_test_recovered==2,"unchanged invalid pointer propagates once");
    VirtualFree(page,0,MEM_RELEASE); m98_InitializeSListHead(&head);
    say("PASS: SList unchanged invalid pointer propagates to caller SEH\r\n");
    sequence_wrap();
    concurrent_free();
    CloseHandle(entered); CloseHandle(changed);
    say("PASS: SList concurrent 6000 page push/pop/free cycles\r\n"); ExitProcess(0);
}
