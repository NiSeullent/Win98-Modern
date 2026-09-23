/* Diagnostic x86 native implementation snapshot, not a compatibility score.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static SLIST_HEADER head;
static DECLSPEC_ALIGN(8) SLIST_ENTRY nodes[4];
static void show(const char *name) {
    char text[64]; DWORD n=0,w,v=head.Depth|((DWORD)head.Sequence<<16),i;
    while (*name) text[n++]=*name++;
    text[n++]=' ';
    for(i=0;i<8;i++) text[n++]="0123456789ABCDEF"[(v>>(28-i*4))&15];
    text[n++]='\r'; text[n++]='\n';
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,n,&w,0);
}
void mainCRTStartup(void) {
    typedef PSLIST_ENTRY (__fastcall *batch_fn)(PSLIST_HEADER,PSLIST_ENTRY,PSLIST_ENTRY,ULONG);
    batch_fn batch=(batch_fn)(ULONG_PTR)GetProcAddress(GetModuleHandleA("kernel32.dll"),"InterlockedPushListSList");
    InitializeSListHead(&head); show("init");
    InterlockedPushEntrySList(&head,nodes); show("push");
    InterlockedPopEntrySList(&head); show("pop");
    InterlockedPushEntrySList(&head,nodes); show("push2");
    InterlockedFlushSList(&head); show("flush");
    head.Depth=65535; head.Sequence=2;
    InterlockedPushEntrySList(&head,nodes); show("push-depth-wrap");
    InterlockedPopEntrySList(&head); show("pop-depth-wrap");
    InitializeSListHead(&head); head.Depth=65535; head.Sequence=2;
    nodes[0].Next=&nodes[2];
    if(batch) { batch(&head,nodes,&nodes[2],2); show("batch-depth-wrap"); }
    else ExitProcess(2);
    ExitProcess(0);
}
