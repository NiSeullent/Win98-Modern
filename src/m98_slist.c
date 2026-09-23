/* x86 SList: shared KERNEL32/NTDLL semantics without an NT kernel dependency.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Source review (pinned archive receipt: build/api-sources/receipt.json):
 * Wine df15af3652511150490934682202d45af892f887, dlls/ntdll/sync.c,
 * Rtl* SList bodies: independent 16-bit depth/sequence, CAS64 mutation and
 * the reclaimed-node exception problem in Pop (LGPL-2.1-or-later).
 * ReactOS 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8:
 * sdk/lib/rtl/slist.c, sdk/lib/rtl/i386/interlck.S, and
 * ntoskrnl/ke/i386/traphdlr.c::KiCheckForSListFault (GPL-2.0-or-later).
 * Its x86 fault/resume labels and header comparison informed the scoped
 * exception recovery below; there is no copied NT kernel trap handler.
 * KernelEx 31cdfc3560fc116637ee8ed7be31b12f3aacf5d1,
 * apilibs/kexbases/Kernel32/inter.c was also reviewed: its flush is a stub,
 * pop is not atomic, and depth reads pointer bits. Those bodies are replaced.
 *
 * The public header is opaque. Wine increments Sequence for every mutation;
 * ReactOS x86 push adds 0x10001 to the high dword, pop subtracts one and flush
 * preserves sequence. Modern WOW64 differs again. We use Wine's explicit
 * field updates (depth modulo 65536, sequence modulo 65536), avoiding carry
 * from depth into sequence. See docs/SLIST_PORT.md for ABA/lifetime limits.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include <stddef.h>
#include "m98_slist.h"

_Static_assert(sizeof(SLIST_HEADER) == 8, "x86 SList header is eight bytes");
_Static_assert(offsetof(SLIST_HEADER, Next) == 0, "x86 next offset");
_Static_assert(offsetof(SLIST_HEADER, Depth) == 4, "x86 depth offset");
_Static_assert(offsetof(SLIST_HEADER, Sequence) == 6, "x86 sequence offset");
_Static_assert(sizeof(SLIST_ENTRY) == 4, "x86 link is a single pointer");

typedef struct { DWORD low, high; } slist_value;

/* Win98's KERNEL32 lacks InterlockedCompareExchange64. CMPXCHG8B is a CPU
 * instruction (Pentium or later), not an OS import. LOCK supplies the full
 * barrier; memory/cc prevent compiler reordering. Failed CAS returns both
 * words together. A compare/exchange of zero with zero is an atomic read. */
static slist_value cas(PSLIST_HEADER head, slist_value old, slist_value next)
{
    __asm__ volatile("lock cmpxchg8b %0"
        : "+m" (head->Alignment), "+a" (old.low), "+d" (old.high)
        : "b" (next.low), "c" (next.high) : "memory", "cc");
    return old;
}
static slist_value snapshot(PSLIST_HEADER head)
{
    slist_value zero = { 0, 0 };
    return cas(head, zero, zero);
}
static DWORD advance(DWORD high, ULONG count)
{
    return ((high + 0x10000UL) & 0xffff0000UL) |
           ((high + count) & 0xffffUL);
}

VOID WINAPI m98_InitializeSListHead(PSLIST_HEADER head)
{
    /* Initialization is not concurrent with other operations, as on Windows. */
    head->Alignment = 0;
}
USHORT WINAPI m98_QueryDepthSList(PSLIST_HEADER head)
{
    return *(volatile USHORT *)&head->Depth;
}
PSLIST_ENTRY WINAPI m98_RtlFirstEntrySList(const SLIST_HEADER *head)
{
    /* This is only a snapshot. It neither removes nor pins the returned node. */
    return *(PSLIST_ENTRY volatile *)&head->Next.Next;
}
PSLIST_ENTRY WINAPI m98_InterlockedPushEntrySList(PSLIST_HEADER head, PSLIST_ENTRY entry)
{
    slist_value old = snapshot(head), next, actual;
    next.low = (DWORD)(ULONG_PTR)entry;
    for (;;) {
        entry->Next = (PSLIST_ENTRY)(ULONG_PTR)old.low;
        next.high = advance(old.high, 1);
        actual = cas(head, old, next);
        if (actual.low == old.low && actual.high == old.high)
            return (PSLIST_ENTRY)(ULONG_PTR)old.low;
        old = actual;
    }
}
PSLIST_ENTRY WINAPI m98_InterlockedFlushSList(PSLIST_HEADER head)
{
    slist_value old = snapshot(head), next, actual;
    next.low = 0;
    while (old.low) {
        next.high = (old.high + 0x10000UL) & 0xffff0000UL;
        actual = cas(head, old, next);
        if (actual.low == old.low && actual.high == old.high) break;
        old = actual;
    }
    return (PSLIST_ENTRY)(ULONG_PTR)old.low;
}
PSLIST_ENTRY WINAPI m98_InterlockedPushListSListEx(PSLIST_HEADER head,
    PSLIST_ENTRY first, PSLIST_ENTRY last, ULONG count)
{
    slist_value old = snapshot(head), next, actual;
    next.low = (DWORD)(ULONG_PTR)first;
    /* A valid caller-owned chain of Count entries is required. Native does
     * not walk/validate it or impose a capacity limit; neither do we. */
    for (;;) {
        last->Next = (PSLIST_ENTRY)(ULONG_PTR)old.low;
        next.high = advance(old.high, count);
        actual = cas(head, old, next);
        if (actual.low == old.low && actual.high == old.high)
            return (PSLIST_ENTRY)(ULONG_PTR)old.low;
        old = actual;
    }
}
PSLIST_ENTRY __fastcall m98_InterlockedPushListSList(PSLIST_HEADER head,
    PSLIST_ENTRY first, PSLIST_ENTRY last, ULONG count)
{
    return m98_InterlockedPushListSListEx(head, first, last, count);
}

/* Manual x86 SEH registration has two ABI words followed by our snapshot.
 * Everything lives on the pop caller's stack; no TLS, heap, spin lock, or
 * per-process registry is involved. Assembly owns the complete frame so an
 * optimizer cannot move FS:[0] changes across the protected dereference. */
typedef struct {
    void *previous;
    void *handler;
    PSLIST_HEADER head;
    DWORD low, high;
} pop_frame;
_Static_assert(sizeof(pop_frame) == 20, "assembly SEH frame layout");
extern char m98_slist_pop_fault[], m98_slist_pop_retry[];
#ifdef M98_SLIST_TEST_HOOK
/* Deterministic race injection belongs to the separate fault-test binary. */
volatile LONG m98_slist_test_recovered;
extern void __cdecl m98_slist_test_before_read(PSLIST_HEADER, PSLIST_ENTRY);
#endif

/* SEH dispatch callbacks use cdecl on x86, independent of WINAPI exports.
 * Never consume an unchanged bad pointer, unrelated fault, or unwind. NT's
 * kernel can restart before demand paging/guard dispatch. Here normal Win98
 * user SEH sees only AV/in-page failure at this one read instruction. */
EXCEPTION_DISPOSITION __cdecl m98_slist_pop_handler(EXCEPTION_RECORD *record,
    void *establisher, CONTEXT *context, void *dispatcher)
{
    pop_frame *frame = (pop_frame *)establisher;
    slist_value current;
    (void)dispatcher;
    if (record->ExceptionFlags ||
        (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION &&
         record->ExceptionCode != EXCEPTION_IN_PAGE_ERROR) ||
        record->ExceptionAddress != m98_slist_pop_fault ||
        context->Eip != (DWORD)(ULONG_PTR)m98_slist_pop_fault ||
        record->NumberParameters < 2 || record->ExceptionInformation[0] != 0 ||
        record->ExceptionInformation[1] != frame->low)
        return ExceptionContinueSearch;
    /* The header itself must remain allocated throughout every API call. */
    current = snapshot(frame->head);
    if (current.low == frame->low && current.high == frame->high)
        return ExceptionContinueSearch;
#ifdef M98_SLIST_TEST_HOOK
    InterlockedIncrement(&m98_slist_test_recovered);
#endif
    context->Eip = (DWORD)(ULONG_PTR)m98_slist_pop_retry;
    return ExceptionContinueExecution;
}

__attribute__((naked)) PSLIST_ENTRY WINAPI
m98_InterlockedPopEntrySList(PSLIST_HEADER head __attribute__((unused)))
{
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\t"
        "pushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $20,%esp\n\tmovl %esp,%esi\n\tmovl 8(%ebp),%edi\n\t"
        "movl %fs:0,%eax\n\tmovl %eax,0(%esi)\n\t"
        "movl $_m98_slist_pop_handler,4(%esi)\n\t"
        "movl %edi,8(%esi)\n\tmovl %esi,%fs:0\n\t"
        ".globl _m98_slist_pop_retry\n_m98_slist_pop_retry:\n\t"
        "xorl %eax,%eax\n\txorl %edx,%edx\n\t"
        "xorl %ebx,%ebx\n\txorl %ecx,%ecx\n\t"
        "lock cmpxchg8b (%edi)\n\t"
        "testl %eax,%eax\n\tjz 2f\n\t"
        "movl %eax,12(%esi)\n\tmovl %edx,16(%esi)\n\t"
#ifdef M98_SLIST_TEST_HOOK
        "pushl %eax\n\tpushl %edi\n\t"
        "call _m98_slist_test_before_read\n\taddl $8,%esp\n\t"
        "movl 12(%esi),%eax\n\tmovl 16(%esi),%edx\n\t"
#endif
        /* Advance sequence while decrementing only the 16-bit depth. */
        "movl %edx,%ecx\n\taddl $0x10000,%ecx\n\tdecw %cx\n\t"
        ".globl _m98_slist_pop_fault\n_m98_slist_pop_fault:\n\t"
        "movl (%eax),%ebx\n\t"
        "lock cmpxchg8b (%edi)\n\tjnz _m98_slist_pop_retry\n\t"
        "2:\n\tmovl 0(%esi),%edx\n\tmovl %edx,%fs:0\n\t"
        "addl $20,%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\t"
        "popl %ebp\n\tretl $4\n\t");
}
