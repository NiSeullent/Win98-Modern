/* x86 sequenced-list API family. GPL-2.0-only. */
#ifndef M98_SLIST_H
#define M98_SLIST_H
#include <windows.h>
#if !defined(__i386__) || defined(_WIN64)
#error This SList implementation requires the 32-bit x86 Windows ABI
#endif

VOID WINAPI m98_InitializeSListHead(PSLIST_HEADER head);
PSLIST_ENTRY WINAPI m98_InterlockedFlushSList(PSLIST_HEADER head);
PSLIST_ENTRY WINAPI m98_InterlockedPopEntrySList(PSLIST_HEADER head);
PSLIST_ENTRY WINAPI m98_InterlockedPushEntrySList(PSLIST_HEADER head, PSLIST_ENTRY entry);
/* This legacy export is FASTCALL on x86. Ex is the distinct stdcall entry. */
PSLIST_ENTRY __fastcall m98_InterlockedPushListSList(PSLIST_HEADER head,
    PSLIST_ENTRY first, PSLIST_ENTRY last, ULONG count);
PSLIST_ENTRY WINAPI m98_InterlockedPushListSListEx(PSLIST_HEADER head,
    PSLIST_ENTRY first, PSLIST_ENTRY last, ULONG count);
USHORT WINAPI m98_QueryDepthSList(PSLIST_HEADER head);
PSLIST_ENTRY WINAPI m98_RtlFirstEntrySList(const SLIST_HEADER *head);

/* Rtl exports share these bodies, header state and calling conventions. */
#define m98_RtlInitializeSListHead m98_InitializeSListHead
#define m98_RtlInterlockedFlushSList m98_InterlockedFlushSList
#define m98_RtlInterlockedPopEntrySList m98_InterlockedPopEntrySList
#define m98_RtlInterlockedPushEntrySList m98_InterlockedPushEntrySList
#define m98_RtlInterlockedPushListSList m98_InterlockedPushListSList
#define m98_RtlInterlockedPushListSListEx m98_InterlockedPushListSListEx
#define m98_RtlQueryDepthSList m98_QueryDepthSList
#endif
