/* Win98/KernelEx thread route observation. Original code, GPL-2.0-only.
 * This is a diagnostic, not a substitute for FLS callback verification. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

typedef unsigned long (__cdecl *beginthreadex_fn)(void *, unsigned,
    unsigned (__stdcall *)(void *), void *, unsigned, unsigned *);
typedef DWORD (WINAPI *fls_alloc_fn)(void (WINAPI *)(void *));
typedef BOOL (WINAPI *fls_set_fn)(DWORD, void *);
typedef BOOL (WINAPI *fls_free_fn)(DWORD);

static fls_set_fn fls_set;
static fls_free_fn fls_free;
static DWORD fls_index = 0xffffffffUL;
static volatile LONG fls_callback_count;
static volatile LONG fls_set_failed;
static DWORD fls_values[4];
static DWORD fls_tids[4];

static void WINAPI fls_callback(void *value)
{
    LONG index = InterlockedIncrement(&fls_callback_count) - 1;
    if (index >= 0 && index < 4) {
        fls_values[index] = (DWORD)(ULONG_PTR)value;
        fls_tids[index] = GetCurrentThreadId();
    }
}

static void set_worker_fls(DWORD value)
{
    if (fls_set && !fls_set(fls_index, (void *)(ULONG_PTR)value))
        fls_set_failed = 1;
}

static void say(const char *s)
{
    DWORD n = 0, written;
    while (s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, NULL);
}

static void hex32(const char *key, DWORD n)
{
    const char digits[] = "0123456789ABCDEF";
    char out[11];
    unsigned i;
    out[0] = '0'; out[1] = 'x';
    for (i = 0; i != 8; ++i) out[i + 2] = digits[(n >> (28 - i * 4)) & 15];
    out[10] = 0;
    say(key); say(out); say("\r\n");
}

static void fail(const char *s)
{
    say("FAIL: "); say(s); say("\r\n");
    ExitProcess(1);
}

static int bounded_name(BYTE *base, DWORD size, DWORD rva, const char *name)
{
    unsigned i;
    if (rva >= size) return 0;
    for (i = 0; i != 96; ++i) {
        unsigned char x, y;
        if (i >= size - rva) return 0;
        x = base[rva + i]; y = (unsigned char)name[i];
        if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
        if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
        if (x != y) return 0;
        if (!x) return 1;
    }
    return 0;
}

static void address_owner(const char *label, DWORD address)
{
    MEMORY_BASIC_INFORMATION mbi;
    char path[MAX_PATH];
    hex32(label, address);
    if (address && VirtualQuery((LPCVOID)(ULONG_PTR)address, &mbi,
                                sizeof(mbi)) == sizeof(mbi)) {
        DWORD n = GetModuleFileNameA((HMODULE)mbi.AllocationBase,
                                     path, sizeof(path));
        if (n && n < sizeof(path)) {
            path[n] = 0;
            say("  owner="); say(path); say("\r\n");
        } else say("  owner=unresolved\r\n");
    } else say("  owner=unresolved\r\n");
}

/* The loader has resolved FirstThunk. OriginalFirstThunk retains names. */
static void inspect_imports(HMODULE module, const char *label)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY directory;
    DWORD image_size, desc_rva;
    unsigned i, found = 0;
    static const char *wanted[] = {
        "CreateThread", "ExitThread", "FreeLibraryAndExitThread"
    };
    say("MODULE: "); say(label); say("\r\n");
    hex32("  base=", (DWORD)(ULONG_PTR)module);
    if (!module) { say("  status=absent\r\n"); return; }
    if ((DWORD)(ULONG_PTR)module >= 0x80000000UL)
        say("  KernelEx_shared_address=yes\r\n");
    else say("  KernelEx_shared_address=no\r\n");
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > 0x100000) {
        say("  status=invalid_dos_header\r\n"); return;
    }
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        say("  status=invalid_nt_header\r\n"); return;
    }
    image_size = nt->OptionalHeader.SizeOfImage;
    if (image_size < 0x1000 || image_size > 0x40000000UL) {
        say("  status=invalid_image_size\r\n"); return;
    }
    directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || directory.VirtualAddress >= image_size ||
        directory.Size > image_size - directory.VirtualAddress) {
        say("  status=no_valid_import_directory\r\n"); return;
    }
    desc_rva = directory.VirtualAddress;
    for (i = 0; i < 256; ++i) {
        IMAGE_IMPORT_DESCRIPTOR *desc;
        DWORD name_rva, lookup_rva, first_rva;
        unsigned j, k;
        if (desc_rva > image_size - sizeof(*desc)) break;
        desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + desc_rva);
        if (!desc->Name && !desc->FirstThunk) break;
        name_rva = desc->Name;
        if (bounded_name(base, image_size, name_rva, "KERNEL32.DLL")) {
            lookup_rva = desc->OriginalFirstThunk;
            first_rva = desc->FirstThunk;
            if (!lookup_rva) {
                say("  status=KERNEL32 OriginalFirstThunk absent\r\n");
                break;
            }
            for (j = 0; j < 2048; ++j) {
                DWORD entry, target;
                IMAGE_THUNK_DATA32 *look, *resolved;
                if (lookup_rva > image_size - sizeof(*look) ||
                    first_rva > image_size - sizeof(*resolved)) break;
                look = (IMAGE_THUNK_DATA32 *)(base + lookup_rva);
                resolved = (IMAGE_THUNK_DATA32 *)(base + first_rva);
                entry = look->u1.AddressOfData;
                if (!entry) break;
                if (!(entry & IMAGE_ORDINAL_FLAG32) &&
                    entry < image_size - sizeof(WORD)) {
                    name_rva = entry + sizeof(WORD);
                    for (k = 0; k < sizeof(wanted) / sizeof(wanted[0]); ++k) {
                        if (bounded_name(base, image_size, name_rva, wanted[k])) {
                            target = resolved->u1.Function;
                            say("  import="); say(wanted[k]); say("\r\n");
                            address_owner("  target=", target);
                            ++found;
                        }
                    }
                }
                lookup_rva += sizeof(*look);
                first_rva += sizeof(*resolved);
            }
        }
        desc_rva += sizeof(*desc);
        if (desc_rva - directory.VirtualAddress >= directory.Size) break;
    }
    hex32("  recognized_import_count=", found);
}

static DWORD WINAPI natural_worker(LPVOID ignored)
{
    (void)ignored;
    set_worker_fls(0xA1UL);
    return 0x1357UL;
}

static DWORD WINAPI explicit_worker(LPVOID ignored)
{
    (void)ignored;
    set_worker_fls(0xB2UL);
    ExitThread(0x2468UL);
    return 0;
}

static unsigned __stdcall crt_worker(void *ignored)
{
    (void)ignored;
    set_worker_fls(0xC3UL);
    return 0x3579U;
}

static void check_callback(const char *stage, LONG count, DWORD value,
                           DWORD thread_id)
{
    say("FLS_STAGE: "); say(stage); say("\r\n");
    hex32("  callback_count=", (DWORD)fls_callback_count);
    if (!fls_set) return;
    if (fls_set_failed || fls_callback_count != count ||
        fls_values[count - 1] != value ||
        fls_tids[count - 1] != thread_id)
        fail("FLS callback count/value/thread on exit path");
}

static void wait_thread(HANDLE thread, DWORD expected, const char *label)
{
    DWORD code = 0;
    if (!thread) fail(label);
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(thread, &code) || code != expected)
        fail(label);
    CloseHandle(thread);
    hex32(label, code);
}

/* Retain a static FreeLibraryAndExitThread import for the IAT probe. */
static VOID (WINAPI * volatile retained_free_exit)(HMODULE, DWORD) =
    FreeLibraryAndExitThread;

void mainCRTStartup(void)
{
    HMODULE kernel = GetModuleHandleA("KERNEL32.DLL");
    HMODULE crt;
    FARPROC dyn;
    beginthreadex_fn begin;
    HANDLE thread;
    unsigned crt_tid = 0;
    DWORD tid = 0;
    unsigned i;
    LONG expected_callbacks = 2;
    fls_alloc_fn fls_alloc;
    FARPROC fls_set_address, fls_free_address;
    static const char *providers[] = {
        "M98WRAP.DLL", "M98WRP13.DLL", "M98WRP14.DLL", "M98WRP15.DLL",
        "M98WRP16.DLL", "M98WRP17.DLL", "M98WRP18.DLL", "M98WRP19.DLL",
        "M98WRP20.DLL"
    };
    if (!retained_free_exit || !kernel) fail("native imports");
    say("THREAD_LIFECYCLE_PROBE_V2\r\n");
    inspect_imports(GetModuleHandleA(NULL), "probe EXE");
    dyn = GetProcAddress(kernel, "CreateThread");
    address_owner("dynamic_CreateThread=", (DWORD)(ULONG_PTR)dyn);
    dyn = GetProcAddress(kernel, "ExitThread");
    address_owner("dynamic_ExitThread=", (DWORD)(ULONG_PTR)dyn);
    dyn = GetProcAddress(kernel, "FreeLibraryAndExitThread");
    address_owner("dynamic_FreeLibraryAndExitThread=", (DWORD)(ULONG_PTR)dyn);
    dyn = GetProcAddress(kernel, "FlsAlloc");
    address_owner("dynamic_FlsAlloc=", (DWORD)(ULONG_PTR)dyn);
    fls_alloc = (fls_alloc_fn)dyn;
    fls_set_address = GetProcAddress(kernel, "FlsSetValue");
    fls_free_address = GetProcAddress(kernel, "FlsFree");
    address_owner("dynamic_FlsSetValue=", (DWORD)(ULONG_PTR)fls_set_address);
    address_owner("dynamic_FlsFree=", (DWORD)(ULONG_PTR)fls_free_address);
    if (fls_alloc && fls_set_address && fls_free_address) {
        fls_set = (fls_set_fn)fls_set_address;
        fls_free = (fls_free_fn)fls_free_address;
        fls_index = fls_alloc(fls_callback);
        if (fls_index == 0xffffffffUL) fail("FlsAlloc");
        hex32("FLS_ROUTE=active index=", fls_index);
    } else say("FLS_ROUTE=absent_or_partial; callback checks skipped\r\n");
    thread = CreateThread(NULL, 0, natural_worker, NULL, 0, &tid);
    wait_thread(thread, 0x1357UL, "native_return_exit_code=");
    check_callback("CreateThread natural return", 1, 0xA1UL, tid);
    thread = CreateThread(NULL, 0, explicit_worker, NULL, 0, &tid);
    wait_thread(thread, 0x2468UL, "native_ExitThread_exit_code=");
    check_callback("CreateThread explicit ExitThread", 2, 0xB2UL, tid);
    crt = LoadLibraryA("MSVCRT.DLL");
    inspect_imports(crt, "system MSVCRT.DLL");
    if (crt) {
        begin = (beginthreadex_fn)GetProcAddress(crt, "_beginthreadex");
        if (begin) {
            thread = (HANDLE)(ULONG_PTR)begin(NULL, 0, crt_worker,
                                               NULL, 0, &crt_tid);
            if (thread) {
                wait_thread(thread, 0x3579UL, "msvcrt_return_exit_code=");
                hex32("msvcrt_thread_id=", crt_tid);
                check_callback("MSVCRT _beginthreadex natural return", 3,
                               0xC3UL, crt_tid);
                expected_callbacks = 3;
            } else say("OBSERVED: _beginthreadex returned zero\r\n");
        } else say("OBSERVED: _beginthreadex export absent\r\n");
        FreeLibrary(crt);
    }
    for (i = 0; i < sizeof(providers) / sizeof(providers[0]); ++i) {
        HMODULE provider = GetModuleHandleA(providers[i]);
        if (provider) inspect_imports(provider, providers[i]);
    }
    if (fls_free) {
        if (!fls_free(fls_index)) fail("FlsFree");
        hex32("FLS_POST_FREE_CALLBACK_COUNT=", (DWORD)fls_callback_count);
        if (fls_callback_count != expected_callbacks)
            fail("late FLS callback at FlsFree");
    }
    say("PASS: native thread primitives; routing is observational\r\n");
    ExitProcess(0);
}
