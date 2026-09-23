/*
 * Windows 98 SE modernization experiment: first KernelEx API library.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 *
 * Source lineage for the adapted functions is in THIRD_PARTY.md.
 * Only Windows 98-era KERNEL32 entry points are imported by this DLL.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "kex_abi.h"

#ifndef ALL_PROCESSOR_GROUPS
#define ALL_PROCESSOR_GROUPS 0xffff
#endif
#ifndef CRITICAL_SECTION_NO_DEBUG_INFO
#define CRITICAL_SECTION_NO_DEBUG_INFO 0x01000000
#endif

/* Vista/Win7 ABI layouts, declared locally so our import target stays 98. */
typedef struct m98_processor_number {
    WORD Group;
    BYTE Number;
    BYTE Reserved;
} m98_processor_number;

typedef struct m98_group_affinity {
    ULONG_PTR Mask;
    WORD Group;
    WORD Reserved[3];
} m98_group_affinity;

typedef struct m98_file_basic_info {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    DWORD FileAttributes;
} m98_file_basic_info;

typedef struct m98_file_standard_info {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    DWORD NumberOfLinks;
    BYTE DeletePending;
    BYTE Directory;
} m98_file_standard_info;

#define M98_FILE_BASIC_INFO_CLASS 0
#define M98_FILE_STANDARD_INFO_CLASS 1
#define M98_APPMODEL_ERROR_NO_PACKAGE 15700

extern const WCHAR wine_casemap_upper[3582];

static WCHAR m98_uppercase(WCHAR ch)
{
    return ch + wine_casemap_upper[wine_casemap_upper[ch >> 8] + (ch & 0xff)];
}

/* Wine CompareStringOrdinal and RtlCompareUnicodeStrings adapted to use
 * Wine's portable Unicode upper-case map already carried by KernelEx. */
static int WINAPI m98_CompareStringOrdinal(const WCHAR *left, int left_len,
                                           const WCHAR *right, int right_len,
                                           BOOL ignore_case)
{
    int i, length;
    WCHAR a, b;
    if (!left || !right || left_len < -1 || right_len < -1 ||
        (ignore_case != FALSE && ignore_case != TRUE)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (left_len < 0) {
        for (left_len = 0; left[left_len]; ++left_len) { }
    }
    if (right_len < 0) {
        for (right_len = 0; right[right_len]; ++right_len) { }
    }
    length = left_len < right_len ? left_len : right_len;
    for (i = 0; i < length; ++i) {
        a = left[i];
        b = right[i];
        if (ignore_case) {
            a = m98_uppercase(a);
            b = m98_uppercase(b);
        }
        if (a < b) return CSTR_LESS_THAN;
        if (a > b) return CSTR_GREATER_THAN;
    }
    if (left_len < right_len) return CSTR_LESS_THAN;
    if (left_len > right_len) return CSTR_GREATER_THAN;
    return CSTR_EQUAL;
}

/* Win98 has no packaged application identity. Wine/ReactOS use this result
 * for ordinary unpackaged processes; this does not emulate app packages. */
static LONG WINAPI m98_GetCurrentPackageId(UINT32 *length, BYTE *buffer)
{
    (void)buffer;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetCurrentPackageInfo(UINT32 flags, UINT32 *length,
                                              BYTE *buffer, UINT32 *count)
{
    (void)flags; (void)buffer; (void)count;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetCurrentPackageFullName(UINT32 *length, WCHAR *name)
{
    (void)name;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetCurrentPackageFamilyName(UINT32 *length, WCHAR *name)
{
    (void)name;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetCurrentPackagePath(UINT32 *length, WCHAR *path)
{
    (void)path;
    if (!length) return ERROR_INVALID_PARAMETER;
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

/* Every process on Win98 is unpackaged. Validate required arguments and
 * process handles before reporting an ordinary unpackaged process.
 * GetExitCodeProcess accepts the same process-query access on Win98. */
static LONG m98_package_result_for_process(HANDLE process, UINT32 *length)
{
    DWORD exit_code;
    if (!process || !length) return ERROR_INVALID_PARAMETER;
    if (!GetExitCodeProcess(process, &exit_code)) return (LONG)GetLastError();
    return M98_APPMODEL_ERROR_NO_PACKAGE;
}

static LONG WINAPI m98_GetPackageFamilyName(HANDLE process,
                                            UINT32 *length, WCHAR *name)
{
    (void)name;
    return m98_package_result_for_process(process, length);
}

static LONG WINAPI m98_GetPackageFullName(HANDLE process,
                                          UINT32 *length, WCHAR *name)
{
    (void)name;
    return m98_package_result_for_process(process, length);
}

static LONG WINAPI m98_GetPackageId(HANDLE process, UINT32 *length,
                                    BYTE *buffer)
{
    (void)buffer;
    return m98_package_result_for_process(process, length);
}

/* Wine's group-selection logic, specialized for Win98's single CPU/group. */
static DWORD WINAPI m98_GetActiveProcessorCount(WORD group)
{
    if (group == 0 || group == ALL_PROCESSOR_GROUPS) return 1;
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}

static WORD WINAPI m98_GetActiveProcessorGroupCount(void)
{
    return 1;
}

static DWORD WINAPI m98_GetMaximumProcessorCount(WORD group)
{
    if (group == 0 || group == ALL_PROCESSOR_GROUPS) return 1;
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}

static WORD WINAPI m98_GetMaximumProcessorGroupCount(void)
{
    return 1;
}

static DWORD WINAPI m98_GetCurrentProcessorNumber(void)
{
    return 0;
}

static void WINAPI m98_GetCurrentProcessorNumberEx(m98_processor_number *number)
{
    if (!number) return;
    number->Group = 0;
    number->Number = 0;
    number->Reserved = 0;
}

static BOOL WINAPI m98_GetNumaHighestNodeNumber(PULONG highest)
{
    if (!highest) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *highest = 0;
    return TRUE;
}

/* One Win98 memory domain. This reports memory visible to Win98, not
 * necessarily all RAM installed in the physical machine. */
static BOOL WINAPI m98_GetNumaAvailableMemoryNode(UCHAR node,
                                                   PULONGLONG available)
{
    MEMORYSTATUS status;
    if (node != 0 || !available) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    status.dwLength = sizeof(status);
    GlobalMemoryStatus(&status);
    *available = status.dwAvailPhys;
    return TRUE;
}

static BOOL WINAPI m98_GetNumaAvailableMemoryNodeEx(USHORT node,
                                                     PULONGLONG available)
{
    if (node != 0 || !available) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return m98_GetNumaAvailableMemoryNode(0, available);
}

static BOOL WINAPI m98_GetNumaProcessorNode(UCHAR processor, PUCHAR node)
{
    if (!node) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (processor != 0) {
        *node = 0xff;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *node = 0;
    return TRUE;
}

static BOOL WINAPI m98_GetNumaProcessorNodeEx(
    const m98_processor_number *processor, PUSHORT node)
{
    if (!processor || !node) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (processor->Group != 0 || processor->Number != 0) {
        *node = 0xffff;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *node = 0;
    return TRUE;
}

static BOOL WINAPI m98_GetNumaNodeProcessorMask(UCHAR node, PULONGLONG mask)
{
    if (!mask || node != 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *mask = 1;
    return TRUE;
}

static BOOL WINAPI m98_GetNumaNodeProcessorMaskEx(USHORT node,
                                                   m98_group_affinity *mask)
{
    int i;
    if (node != 0 || !mask) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    mask->Mask = 1;
    mask->Group = 0;
    for (i = 0; i < 3; ++i) mask->Reserved[i] = 0;
    return TRUE;
}

/* A Win98 process can only run in processor group 0. Preserve the modern
 * buffer-size protocol so callers can make their usual two-call query. */
static BOOL WINAPI m98_GetProcessGroupAffinity(HANDLE process,
                                               PUSHORT group_count,
                                               PUSHORT group_array)
{
    DWORD exit_code;
    if (!GetExitCodeProcess(process, &exit_code)) return FALSE;
    if (!group_count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (*group_count == 0) {
        *group_count = 1;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (!group_array) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    group_array[0] = 0;
    *group_count = 1;
    return TRUE;
}

/* Win98 boots through a BIOS interface, including on a UEFI machine using
 * its compatibility boot path. It does not enable operating-system DEP. */
static BOOL WINAPI m98_GetFirmwareType(DWORD *firmware_type)
{
    if (!firmware_type) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *firmware_type = 1; /* FirmwareTypeBios */
    return TRUE;
}

static DWORD WINAPI m98_GetSystemDEPPolicy(void)
{
    return 0; /* AlwaysOff */
}

/* ReactOS's RtlInitializeCriticalSectionEx flag checks, with Win9x's
 * uniprocessor spin-count behavior. NO_DEBUG_INFO is advisory here. */
static BOOL WINAPI m98_InitializeCriticalSectionEx(LPCRITICAL_SECTION section,
                                                    DWORD spin_count,
                                                    DWORD flags)
{
    if (!section || (flags & ~CRITICAL_SECTION_NO_DEBUG_INFO) ||
        (spin_count & 0xff000000UL)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    InitializeCriticalSection(section);
    return TRUE;
}

/* SRWLOCK and INIT_ONCE are each one pointer wide in the 32-bit ABI.  A
 * single Win98 interlocked word is sufficient for their basic contracts.
 * Sources checked: Wine dlls/ntdll/sync.c (df15af3) and ReactOS
 * sdk/lib/rtl/srw.c (9dc3ca8).  Their writer-waiter/owner split and atomic
 * transitions inform this new Win98 layout.  Bits 0..14 count readers, bit 15
 * marks an exclusive owner, and bits 16..31 count waiting writers.  Pending
 * writers stop new readers, avoiding writer starvation.  Contended waits
 * sleep for a tick so a lower-priority owner can run on the one CPU.
 * Like Windows SRW locks, these locks are not recursive. */
typedef struct m98_srwlock {
    volatile LONG state;
} m98_srwlock;

#define M98_SRW_READERS 0x00007fffUL
#define M98_SRW_EXCLUSIVE 0x00008000UL
#define M98_SRW_WRITER 0x00010000UL
#define M98_SRW_WRITERS 0xffff0000UL

typedef struct m98_init_once {
    volatile LONG state;
} m98_init_once;

typedef BOOL (WINAPI *m98_init_once_callback)(m98_init_once *, void *, void **);

static void WINAPI m98_InitializeSRWLock(m98_srwlock *lock)
{
    lock->state = 0;
}

static DWORD m98_srw_state(m98_srwlock *lock)
{
    return (DWORD)InterlockedCompareExchange(&lock->state, 0, 0);
}

static BOOLEAN WINAPI m98_TryAcquireSRWLockExclusive(m98_srwlock *lock)
{
    DWORD old_state;
    for (;;) {
        old_state = m98_srw_state(lock);
        if (old_state & (M98_SRW_READERS | M98_SRW_EXCLUSIVE)) return FALSE;
        if ((DWORD)InterlockedCompareExchange(&lock->state,
                (LONG)(old_state | M98_SRW_EXCLUSIVE), (LONG)old_state) == old_state)
            return TRUE;
    }
}

static void WINAPI m98_AcquireSRWLockExclusive(m98_srwlock *lock)
{
    DWORD old_state;
    /* Register before waiting, so arriving readers leave room for writers. */
    for (;;) {
        old_state = m98_srw_state(lock);
        if ((old_state & M98_SRW_WRITERS) != M98_SRW_WRITERS &&
            (DWORD)InterlockedCompareExchange(&lock->state,
                (LONG)(old_state + M98_SRW_WRITER), (LONG)old_state) == old_state)
            break;
        Sleep(1);
    }
    for (;;) {
        old_state = m98_srw_state(lock);
        if (!(old_state & (M98_SRW_READERS | M98_SRW_EXCLUSIVE)) &&
            (DWORD)InterlockedCompareExchange(&lock->state,
                (LONG)((old_state - M98_SRW_WRITER) | M98_SRW_EXCLUSIVE),
                (LONG)old_state) == old_state)
            return;
        Sleep(1);
    }
}

static void WINAPI m98_ReleaseSRWLockExclusive(m98_srwlock *lock)
{
    DWORD old_state;
    do {
        old_state = m98_srw_state(lock);
    } while ((DWORD)InterlockedCompareExchange(&lock->state,
                 (LONG)(old_state & ~M98_SRW_EXCLUSIVE),
                 (LONG)old_state) != old_state);
}

static BOOLEAN WINAPI m98_TryAcquireSRWLockShared(m98_srwlock *lock)
{
    DWORD old_state;
    for (;;) {
        old_state = m98_srw_state(lock);
        if ((old_state & (M98_SRW_WRITERS | M98_SRW_EXCLUSIVE)) ||
            (old_state & M98_SRW_READERS) == M98_SRW_READERS)
            return FALSE;
        if ((DWORD)InterlockedCompareExchange(&lock->state,
                (LONG)(old_state + 1), (LONG)old_state) == old_state)
            return TRUE;
    }
}

static void WINAPI m98_AcquireSRWLockShared(m98_srwlock *lock)
{
    while (!m98_TryAcquireSRWLockShared(lock)) Sleep(1);
}

static void WINAPI m98_ReleaseSRWLockShared(m98_srwlock *lock)
{
    DWORD old_state;
    do {
        old_state = m98_srw_state(lock);
    } while ((DWORD)InterlockedCompareExchange(&lock->state,
                 (LONG)(old_state - 1), (LONG)old_state) != old_state);
}

/* Wine dlls/ntdll/sync.c (df15af3) RtlRunOnce state tags: 0 uninitialized,
 * 1 running, and 2 plus an
 * aligned context when complete.  Callbacks that fail reset state for retry.
 * The callback's context pointer must have its low two bits clear. */
static BOOL WINAPI m98_InitOnceExecuteOnce(m98_init_once *once,
                                            m98_init_once_callback callback,
                                            void *parameter, void **context)
{
    LONG state;
    void *result;
    BOOL success;
    if (!once || !callback) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (;;) {
        state = InterlockedCompareExchange(&once->state, 1, 0);
        if (state == 0) {
            success = callback(once, parameter, context);
            result = context ? *context : 0;
            if (success && ((ULONG_PTR)result & 3)) {
                SetLastError(ERROR_INVALID_PARAMETER);
                success = FALSE;
            }
            if (success) {
                InterlockedExchange(&once->state, (LONG)((ULONG_PTR)result | 2));
            } else {
                InterlockedExchange(&once->state, 0);
            }
            return success;
        }
        if ((state & 3) == 2) {
            if (context) *context = (void *)(ULONG_PTR)(state & ~3L);
            return TRUE;
        }
        Sleep(1);
    }
}

/* GetTickCount is 32-bit on Win98. Extend it while this process is alive.
 * The high word is unknowable if the DLL first loads after an uptime wrap. */
static CRITICAL_SECTION tick_lock;
static DWORD last_tick;
static ULONGLONG tick_high;

static ULONGLONG WINAPI m98_GetTickCount64(void)
{
    DWORD current;
    ULONGLONG result;
    EnterCriticalSection(&tick_lock);
    current = GetTickCount();
    if (current < last_tick) tick_high += 0x100000000ULL;
    last_tick = current;
    result = tick_high | current;
    LeaveCriticalSection(&tick_lock);
    return result;
}

/* Import bridge only: Win98 cannot provide the sub-microsecond precision
 * promised by the Windows 8 API. */
static void WINAPI m98_GetSystemTimePreciseAsFileTime(LPFILETIME time)
{
    GetSystemTimeAsFileTime(time);
}

/* Basic and standard file information can be derived from a Win98 handle.
 * ChangeTime is approximated by LastWriteTime; allocation size by EOF. */
static BOOL WINAPI m98_GetFileInformationByHandleEx(HANDLE file,
                                                     int kind,
                                                     LPVOID output,
                                                     DWORD output_size)
{
    BY_HANDLE_FILE_INFORMATION source;
    if (!output) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (kind == M98_FILE_BASIC_INFO_CLASS) {
        m98_file_basic_info *basic;
        if (output_size < sizeof(m98_file_basic_info)) {
            SetLastError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        if (!GetFileInformationByHandle(file, &source)) return FALSE;
        basic = (m98_file_basic_info *)output;
        basic->CreationTime.LowPart = source.ftCreationTime.dwLowDateTime;
        basic->CreationTime.HighPart = source.ftCreationTime.dwHighDateTime;
        basic->LastAccessTime.LowPart = source.ftLastAccessTime.dwLowDateTime;
        basic->LastAccessTime.HighPart = source.ftLastAccessTime.dwHighDateTime;
        basic->LastWriteTime.LowPart = source.ftLastWriteTime.dwLowDateTime;
        basic->LastWriteTime.HighPart = source.ftLastWriteTime.dwHighDateTime;
        basic->ChangeTime = basic->LastWriteTime;
        basic->FileAttributes = source.dwFileAttributes;
        return TRUE;
    }
    if (kind == M98_FILE_STANDARD_INFO_CLASS) {
        m98_file_standard_info *standard;
        if (output_size < sizeof(m98_file_standard_info)) {
            SetLastError(ERROR_BAD_LENGTH);
            return FALSE;
        }
        if (!GetFileInformationByHandle(file, &source)) return FALSE;
        standard = (m98_file_standard_info *)output;
        standard->EndOfFile.LowPart = source.nFileSizeLow;
        standard->EndOfFile.HighPart = source.nFileSizeHigh;
        standard->AllocationSize = standard->EndOfFile;
        standard->NumberOfLinks = source.nNumberOfLinks;
        standard->DeletePending = FALSE;
        standard->Directory = (source.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return TRUE;
    }
    /* GetFileInformationByHandleEx rejects invalid and set-only classes.
     * Keep valid query classes distinguishable from unsupported values so
     * callers probing for a feature can handle the failure correctly. */
    switch (kind) {
    case 2:  /* FileNameInfo */
    case 7:  /* FileStreamInfo */
    case 8:  /* FileCompressionInfo */
    case 9:  /* FileAttributeTagInfo */
    case 10: /* FileIdBothDirectoryInfo */
    case 11: /* FileIdBothDirectoryRestartInfo */
    case 13: /* FileRemoteProtocolInfo */
    case 14: /* FileFullDirectoryInfo */
    case 15: /* FileFullDirectoryRestartInfo */
    case 16: /* FileStorageInfo */
    case 17: /* FileAlignmentInfo */
    case 18: /* FileIdInfo */
    case 19: /* FileIdExtdDirectoryInfo */
    case 20: /* FileIdExtdDirectoryRestartInfo */
    case 23: /* FileCaseSensitiveInfo */
    case 24: /* FileNormalizedNameInfo */
        SetLastError(ERROR_NOT_SUPPORTED);
        break;
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        break;
    }
    return FALSE;
}

/* Names must stay sorted: KernelEx validates this before loading an API lib. */
#define M98_API(name, impl) { name, (unsigned long)(impl) }
static const m98_named_api kernel32_apis[] = {
    M98_API("AcquireSRWLockExclusive", m98_AcquireSRWLockExclusive),
    M98_API("AcquireSRWLockShared", m98_AcquireSRWLockShared),
    M98_API("CompareStringOrdinal", m98_CompareStringOrdinal),
    M98_API("GetActiveProcessorCount", m98_GetActiveProcessorCount),
    M98_API("GetActiveProcessorGroupCount", m98_GetActiveProcessorGroupCount),
    M98_API("GetCurrentPackageFamilyName", m98_GetCurrentPackageFamilyName),
    M98_API("GetCurrentPackageFullName", m98_GetCurrentPackageFullName),
    M98_API("GetCurrentPackageId", m98_GetCurrentPackageId),
    M98_API("GetCurrentPackageInfo", m98_GetCurrentPackageInfo),
    M98_API("GetCurrentPackagePath", m98_GetCurrentPackagePath),
    M98_API("GetCurrentProcessorNumber", m98_GetCurrentProcessorNumber),
    M98_API("GetCurrentProcessorNumberEx", m98_GetCurrentProcessorNumberEx),
    M98_API("GetFileInformationByHandleEx", m98_GetFileInformationByHandleEx),
    M98_API("GetFirmwareType", m98_GetFirmwareType),
    M98_API("GetMaximumProcessorCount", m98_GetMaximumProcessorCount),
    M98_API("GetMaximumProcessorGroupCount", m98_GetMaximumProcessorGroupCount),
    M98_API("GetNumaAvailableMemoryNode", m98_GetNumaAvailableMemoryNode),
    M98_API("GetNumaAvailableMemoryNodeEx", m98_GetNumaAvailableMemoryNodeEx),
    M98_API("GetNumaHighestNodeNumber", m98_GetNumaHighestNodeNumber),
    M98_API("GetNumaNodeProcessorMask", m98_GetNumaNodeProcessorMask),
    M98_API("GetNumaNodeProcessorMaskEx", m98_GetNumaNodeProcessorMaskEx),
    M98_API("GetNumaProcessorNode", m98_GetNumaProcessorNode),
    M98_API("GetNumaProcessorNodeEx", m98_GetNumaProcessorNodeEx),
    M98_API("GetPackageFamilyName", m98_GetPackageFamilyName),
    M98_API("GetPackageFullName", m98_GetPackageFullName),
    M98_API("GetPackageId", m98_GetPackageId),
    M98_API("GetProcessGroupAffinity", m98_GetProcessGroupAffinity),
    M98_API("GetSystemDEPPolicy", m98_GetSystemDEPPolicy),
    M98_API("GetSystemTimePreciseAsFileTime", m98_GetSystemTimePreciseAsFileTime),
    M98_API("GetTickCount64", m98_GetTickCount64),
    M98_API("InitOnceExecuteOnce", m98_InitOnceExecuteOnce),
    M98_API("InitializeCriticalSectionEx", m98_InitializeCriticalSectionEx),
    M98_API("InitializeSRWLock", m98_InitializeSRWLock),
    M98_API("ReleaseSRWLockExclusive", m98_ReleaseSRWLockExclusive),
    M98_API("ReleaseSRWLockShared", m98_ReleaseSRWLockShared),
    M98_API("TryAcquireSRWLockExclusive", m98_TryAcquireSRWLockExclusive),
    M98_API("TryAcquireSRWLockShared", m98_TryAcquireSRWLockShared)
};

static const m98_api_table api_tables[] = {
    { "KERNEL32.DLL", kernel32_apis,
      sizeof(kernel32_apis) / sizeof(kernel32_apis[0]), 0, 0 },
    { 0, 0, 0, 0, 0 }
};

__declspec(dllexport) const m98_api_table *get_api_table(void)
{
    return api_tables;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&tick_lock);
        last_tick = GetTickCount();
        tick_high = 0;
    } else if (reason == DLL_PROCESS_DETACH) {
        DeleteCriticalSection(&tick_lock);
    }
    return TRUE;
}
