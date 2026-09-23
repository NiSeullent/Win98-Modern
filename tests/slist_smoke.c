/* Public-contract suite for fixture, integrated table, and static imports.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"
#ifdef InterlockedPushListSList
#undef InterlockedPushListSList
#endif
typedef VOID (WINAPI *init_fn)(PSLIST_HEADER);
typedef PSLIST_ENTRY (WINAPI *single_fn)(PSLIST_HEADER);
typedef PSLIST_ENTRY (WINAPI *push_fn)(PSLIST_HEADER,PSLIST_ENTRY);
typedef PSLIST_ENTRY (__fastcall *batch_fn)(PSLIST_HEADER,PSLIST_ENTRY,PSLIST_ENTRY,ULONG);
typedef PSLIST_ENTRY (WINAPI *batch_ex_fn)(PSLIST_HEADER,PSLIST_ENTRY,PSLIST_ENTRY,ULONG);
typedef USHORT (WINAPI *depth_fn)(PSLIST_HEADER);
typedef PSLIST_ENTRY (WINAPI *first_fn)(const SLIST_HEADER *);
static init_fn initialize;
static single_fn pop, flush;
static push_fn push;
static batch_fn batch;
static batch_ex_fn batch_ex;
static depth_fn depth;
static first_fn first;
#ifdef M98_STATIC_IMPORT
__declspec(dllimport) PSLIST_ENTRY __fastcall InterlockedPushListSList(
    PSLIST_HEADER,PSLIST_ENTRY,PSLIST_ENTRY,ULONG);
__declspec(dllimport) PSLIST_ENTRY WINAPI InterlockedPushListSListEx(
    PSLIST_HEADER,PSLIST_ENTRY,PSLIST_ENTRY,ULONG);
#endif
typedef struct DECLSPEC_ALIGN(8) {
    SLIST_ENTRY link;
    volatile LONG owner;
    DWORD index, payload;
} node;
static void say(const char *s) {
    DWORD n=0,w; while(s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,n,&w,0);
}
static void require(BOOL condition, const char *what) {
    if(!condition) { say("FAIL: "); say(what); say("\r\n"); ExitProcess(1); }
}
static void hex(DWORD value) {
    char text[11]="0x00000000"; DWORD i;
    for(i=0;i<8;++i) text[9-i]="0123456789ABCDEF"[(value>>(i*4))&15];
    say(text);
}
#ifndef M98_STATIC_IMPORT
static ULONG_PTR lookup(const m98_api_table *table, const char *dll, const char *name) {
    int i;
    for(;table->target_library;++table) if(!lstrcmpiA(table->target_library,dll))
        for(i=0;i<table->named_apis_count;++i)
            if(!lstrcmpA(table->named_apis[i].name,name)) return table->named_apis[i].addr;
    return 0;
}
#endif
static void load(void) {
#ifdef M98_STATIC_IMPORT
    initialize=InitializeSListHead; pop=InterlockedPopEntrySList;
    flush=InterlockedFlushSList; push=InterlockedPushEntrySList;
    batch=InterlockedPushListSList; batch_ex=InterlockedPushListSListEx;
    depth=QueryDepthSList;
#else
    typedef const m98_api_table *(__cdecl *table_fn)(void);
    const m98_api_table *table; table_fn get;
#ifdef M98_INTEGRATED
    HMODULE module=LoadLibraryA("M98WRAP.DLL");
#else
    HMODULE module=LoadLibraryA("SLISTFIX.DLL");
#endif
    require(module!=0,"load SList API provider");
    get=(table_fn)(ULONG_PTR)GetProcAddress(module,"get_api_table");
    require(get!=0,"get_api_table"); table=get();
    initialize=(init_fn)lookup(table,"KERNEL32.DLL","InitializeSListHead");
    pop=(single_fn)lookup(table,"KERNEL32.DLL","InterlockedPopEntrySList");
    flush=(single_fn)lookup(table,"KERNEL32.DLL","InterlockedFlushSList");
    push=(push_fn)lookup(table,"KERNEL32.DLL","InterlockedPushEntrySList");
    batch=(batch_fn)lookup(table,"KERNEL32.DLL","InterlockedPushListSList");
    batch_ex=(batch_ex_fn)lookup(table,"KERNEL32.DLL","InterlockedPushListSListEx");
    depth=(depth_fn)lookup(table,"KERNEL32.DLL","QueryDepthSList");
    first=(first_fn)lookup(table,"NTDLL.DLL","RtlFirstEntrySList");
#ifndef M98_INTEGRATED
    require(first!=0,"RtlFirstEntrySList fixture export");
    require((ULONG_PTR)pop==lookup(table,"NTDLL.DLL","RtlInterlockedPopEntrySList") &&
        (ULONG_PTR)initialize==lookup(table,"NTDLL.DLL","RtlInitializeSListHead") &&
        (ULONG_PTR)flush==lookup(table,"NTDLL.DLL","RtlInterlockedFlushSList") &&
        (ULONG_PTR)push==lookup(table,"NTDLL.DLL","RtlInterlockedPushEntrySList") &&
        (ULONG_PTR)batch==lookup(table,"NTDLL.DLL","RtlInterlockedPushListSList") &&
        (ULONG_PTR)batch_ex==lookup(table,"NTDLL.DLL","RtlInterlockedPushListSListEx") &&
        (ULONG_PTR)depth==lookup(table,"NTDLL.DLL","RtlQueryDepthSList"),
        "Rtl aliases share all seven implementations");
#endif
#endif
    require(initialize&&pop&&flush&&push&&batch&&batch_ex&&depth,"seven KERNEL32 SList APIs");
}
static void basic(void) {
    DECLSPEC_ALIGN(8) SLIST_HEADER head;
    node n[4]; PSLIST_ENTRY p; ULONG i;
    require(sizeof(head)==8 && !((ULONG_PTR)&head&7) && !((ULONG_PTR)n&7),"x86 header/entry alignment");
    initialize(&head); SetLastError(0x314159);
    require(!pop(&head)&&!flush(&head)&&!depth(&head),"empty results");
    require(!push(&head,&n[0].link),"first push returns NULL");
    require(push(&head,&n[1].link)==&n[0].link,"push returns prior first");
    if(first) require(first(&head)==&n[1].link,"FirstEntry snapshot");
    require(depth(&head)==2&&pop(&head)==&n[1].link&&pop(&head)==&n[0].link,"LIFO and depth");
    require(!pop(&head),"drained empty");
    for(i=0;i<12000;++i) {
        n[0].link.Next=&n[1].link;
        require(!batch(&head,&n[0].link,&n[1].link,2),"fastcall batch return");
        n[2].link.Next=&n[3].link;
        require(batch_ex(&head,&n[2].link,&n[3].link,2)==&n[0].link,"stdcall batch return");
        require(depth(&head)==4,"batch depth"); p=flush(&head);
        require(p==&n[2].link&&p->Next==&n[3].link&&p->Next->Next==&n[0].link&&
            n[0].link.Next==&n[1].link&&!n[1].link.Next,"flush preserves complete chain");
    }
    require(!depth(&head)&&!pop(&head),"empty after repeated ABI calls");
    require(GetLastError()==0x314159,"SList preserves caller last-error");
    say("PASS: SList alignment/LIFO/fastcall+stdcall batch/flush\r\n");
}
static void depth_wrap(void) {
    DECLSPEC_ALIGN(8) SLIST_HEADER head; DWORD i;
    node *n;
    /* Win98's native heap may reject a block of this size or supply only
     * four-byte alignment. Diagnose those separately; neither is SList's
     * list-capacity contract. VirtualAlloc supplies page-aligned test storage. */
    void *heap_probe=HeapAlloc(GetProcessHeap(),0,65537*sizeof(node));
    if(!heap_probe) { say("INFO: large HeapAlloc failed, last-error="); hex(GetLastError()); say("\r\n"); }
    else {
        say("INFO: large HeapAlloc alignment offset="); hex((DWORD)(ULONG_PTR)heap_probe&7); say("\r\n");
        HeapFree(GetProcessHeap(),0,heap_probe);
    }
    n=VirtualAlloc(0,65537*sizeof(node),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!n) { say("INFO: large VirtualAlloc failed, last-error="); hex(GetLastError()); say("\r\n"); }
    require(n!=0,"large page allocation");
    require(!((ULONG_PTR)n&7),"page allocation satisfies SList alignment");
    initialize(&head);
    for(i=0;i<65536;++i) n[i].link.Next=&n[i+1].link;
    require(!batch_ex(&head,&n[0].link,&n[65536].link,65537)&&depth(&head)==1,"batch count exceeds 16-bit depth");
    require(pop(&head)==&n[0].link&&depth(&head)==0,"depth wraps while nonempty");
    require(pop(&head)==&n[1].link&&depth(&head)==65535,"pop depth underflow modulo16");
    for(i=2;i<65537;++i) require(pop(&head)==&n[i].link,"large chain integrity");
    require(!pop(&head)&&!depth(&head),"large chain fully drained");
    for(i=0;i<65535;++i) n[i].link.Next=&n[i+1].link;
    batch(&head,&n[0].link,&n[65535].link,65536);
    require(!depth(&head)&&flush(&head)==&n[0].link&&!pop(&head),"flush must use next, not wrapped depth");
    require(VirtualFree(n,0,MEM_RELEASE),"large page allocation release");
    say("PASS: SList depth wrap/65537-node chain\r\n");
}
#define POOL 128
#define PRODUCERS 4
#define CONSUMERS 4
#define ROUNDS 10000
static DECLSPEC_ALIGN(8) SLIST_HEADER free_list, work_list;
static node pool[POOL];
static HANDLE start_event;
static volatile LONG producers_done, consumed;
static DWORD WINAPI producer(void *arg) {
    DWORD i; node *n; DWORD salt=(DWORD)(ULONG_PTR)arg;
    WaitForSingleObject(start_event,INFINITE);
    for(i=0;i<ROUNDS;++i) {
        while(!(n=(node *)pop(&free_list))) Sleep(0);
        require(InterlockedCompareExchange(&n->owner,1,0)==0,"producer owns unique recycled node");
        n->payload=n->index ^ (i+salt);
        /* index stays immutable; owner publishes the payload generation. */
        n->owner=(LONG)(i+salt+2);
        push(&work_list,&n->link);
    }
    InterlockedIncrement(&producers_done); return 0;
}
static DWORD WINAPI consumer(void *arg) {
    node *n; LONG generation;
    (void)arg; WaitForSingleObject(start_event,INFINITE);
    for(;;) {
        n=(node *)pop(&work_list);
        if(!n) {
            if(InterlockedCompareExchange(&producers_done,0,0)==PRODUCERS &&
               InterlockedCompareExchange(&consumed,0,0)==PRODUCERS*ROUNDS) break;
            Sleep(0); continue;
        }
        generation=InterlockedExchange(&n->owner,-1);
        require(generation>=2&&n->index<POOL&&
            n->payload==(n->index^(DWORD)(generation-2)),"consumer unique ownership/payload visibility");
        InterlockedIncrement(&consumed);
        InterlockedExchange(&n->owner,0);
        push(&free_list,&n->link);
    }
    return 0;
}
static void concurrent(void) {
    HANDLE workers[PRODUCERS+CONSUMERS]; DWORD i,count=0; BOOL seen[POOL]; node *n;
    initialize(&free_list); initialize(&work_list);
    for(i=0;i<POOL;++i) { seen[i]=FALSE; pool[i].index=i; pool[i].owner=0; push(&free_list,&pool[i].link); }
    start_event=CreateEventA(0,TRUE,FALSE,0); require(start_event!=0,"start event");
    for(i=0;i<PRODUCERS+CONSUMERS;++i) {
        workers[i]=CreateThread(0,0,i<PRODUCERS?producer:consumer,(void *)(ULONG_PTR)(i*ROUNDS),0,0);
        require(workers[i]!=0,"concurrent worker creation");
    }
    SetEvent(start_event);
    require(WaitForMultipleObjects(PRODUCERS+CONSUMERS,workers,TRUE,120000)==WAIT_OBJECT_0,"eight workers complete");
    for(i=0;i<PRODUCERS+CONSUMERS;++i) CloseHandle(workers[i]);
    CloseHandle(start_event);
    require(consumed==PRODUCERS*ROUNDS&&!depth(&work_list)&&!pop(&work_list),"all published items consumed");
    while((n=(node *)pop(&free_list))) {
        require(n->index<POOL&&!seen[n->index]&&n->owner==0,"each pool node returned once");
        seen[n->index]=TRUE; ++count;
    }
    require(count==POOL,"no lost nodes after concurrent reuse");
    say("PASS: SList concurrent 4 producers/4 consumers/40000 reuse transfers\r\n");
}
void mainCRTStartup(void) {
    load(); basic(); depth_wrap(); concurrent();
    say("PASS: SList seven-API public-contract suite\r\n"); ExitProcess(0);
}
