/* Clipboard listener/format contract probe. GPL-2.0-only.
 * Default mode never changes clipboard contents. --guest-mutate is guarded
 * to Windows 98 and refuses unknown preexisting formats before mutation. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

#ifdef M98_STATIC_CLIPBOARD_IMPORTS
/* These Vista-era names are intentionally imported from USER32. The guest
 * loader must resolve them through KernelEx before this executable starts. */
__declspec(dllimport) BOOL WINAPI AddClipboardFormatListener(HWND);
__declspec(dllimport) BOOL WINAPI RemoveClipboardFormatListener(HWND);
__declspec(dllimport) BOOL WINAPI GetUpdatedClipboardFormats(PUINT, UINT, PUINT);
#endif

#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031d
#endif
#ifndef ERROR_INVALID_WINDOW_HANDLE
#define ERROR_INVALID_WINDOW_HANDLE 1400
#endif
#ifndef ERROR_NOACCESS
#define ERROR_NOACCESS 998
#endif

typedef BOOL (WINAPI *add_fn)(HWND);
typedef BOOL (WINAPI *remove_fn)(HWND);
typedef BOOL (WINAPI *formats_fn)(PUINT, UINT, PUINT);
typedef const m98_api_table *(__cdecl *table_fn)(void);

static add_fn add_listener;
static remove_fn remove_listener;
static formats_fn updated_formats;
static HMODULE provider;
static volatile LONG update_count, draw_count, bad_update_params;
static volatile LONG event_serial, first_update_event, first_draw_event;
static HWND next_viewer;
static DWORD failures;
static UINT large_formats[1024];
static BOOL guest_is_win98(void);

static void say(const char *s)
{
    DWORD n = 0, written;
    while (s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, NULL);
}

static void hex32(DWORD value)
{
    char text[11] = "0x00000000";
    unsigned int i;
    for (i = 0; i < 8; ++i)
        text[9 - i] = "0123456789ABCDEF"[(value >> (i * 4)) & 15];
    say(text);
}

static void report(const char *name, BOOL result, DWORD error)
{
    say(name); say(" result="); hex32((DWORD)result);
    say(" error="); hex32(error); say("\r\n");
}

static void require(BOOL condition, const char *why)
{
    if (!condition) {
        ++failures;
        say("FAIL: "); say(why); say("\r\n");
    }
}

static BOOL has_option(const char *cmd, const char *option)
{
    const char *p;
    for (; *cmd; ++cmd) {
        if (*cmd != '-' || (cmd > GetCommandLineA() && cmd[-1] != ' '))
            continue;
        p = option;
        while (*p && cmd[p - option] == *p) ++p;
        if (!*p && (cmd[p - option] == 0 || cmd[p - option] == ' ')) return TRUE;
    }
    return FALSE;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_CLIPBOARDUPDATE) {
        LONG order = InterlockedIncrement(&event_serial);
        if (!first_update_event) first_update_event = order;
        InterlockedIncrement(&update_count);
        if (wparam || lparam) InterlockedIncrement(&bad_update_params);
        return 0;
    }
    if (msg == WM_DRAWCLIPBOARD) {
        DWORD_PTR ignored = 0;
        LONG order = InterlockedIncrement(&event_serial);
        if (!first_draw_event) first_draw_event = order;
        InterlockedIncrement(&draw_count);
        if (next_viewer && next_viewer != hwnd)
            SendMessageTimeoutA(next_viewer, msg, wparam, lparam,
                                SMTO_ABORTIFHUNG, 250, &ignored);
        return 0;
    }
    if (msg == WM_CHANGECBCHAIN) {
        if ((HWND)wparam == next_viewer) next_viewer = (HWND)lparam;
        else if (next_viewer && next_viewer != hwnd) {
            DWORD_PTR ignored = 0;
            SendMessageTimeoutA(next_viewer, msg, wparam, lparam,
                                SMTO_ABORTIFHUNG, 250, &ignored);
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static HWND create_test_window(void)
{
    return CreateWindowExA(0, "M98ClipboardContract", "M98ClipboardContract",
                           WS_POPUP, 0, 0, 1, 1, NULL, NULL,
                           GetModuleHandleA(NULL), NULL);
}

static void pump_messages(DWORD millis)
{
    DWORD start = GetTickCount();
    MSG msg;
    do {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(5);
    } while (GetTickCount() - start < millis);
}

#ifndef M98_STATIC_CLIPBOARD_IMPORTS
static void *lookup(const m98_api_table *table, const char *name)
{
    int i;
    for (; table && table->target_library; ++table) {
        if (lstrcmpiA(table->target_library, "USER32.DLL")) continue;
        for (i = 0; i < table->named_apis_count; ++i)
            if (!lstrcmpA(table->named_apis[i].name, name))
                return (void *)(ULONG_PTR)table->named_apis[i].addr;
    }
    return NULL;
}
#endif

static BOOL load_apis(BOOL bridge, BOOL shipping)
{
#ifdef M98_STATIC_CLIPBOARD_IMPORTS
    (void)bridge;
    (void)shipping;
    provider = GetModuleHandleA("USER32.DLL");
    add_listener = AddClipboardFormatListener;
    remove_listener = RemoveClipboardFormatListener;
    updated_formats = GetUpdatedClipboardFormats;
    say("OBSERVED: static USER32 clipboard imports bound\r\n");
#else
    if (bridge) {
        table_fn get_table;
        const m98_api_table *table;
        provider = LoadLibraryA(shipping ? "M98USER.DLL" : "CLIPFIX.DLL");
        if (!provider) return FALSE;
        get_table = (table_fn)GetProcAddress(provider, "get_api_table");
        if (!get_table) return FALSE;
        table = get_table();
        add_listener = (add_fn)lookup(table, "AddClipboardFormatListener");
        remove_listener = (remove_fn)lookup(table, "RemoveClipboardFormatListener");
        updated_formats = (formats_fn)lookup(table, "GetUpdatedClipboardFormats");
    } else {
        provider = GetModuleHandleA("USER32.DLL");
        if (!provider) return FALSE;
        add_listener = (add_fn)GetProcAddress(provider, "AddClipboardFormatListener");
        remove_listener = (remove_fn)GetProcAddress(provider, "RemoveClipboardFormatListener");
        updated_formats = (formats_fn)GetProcAddress(provider, "GetUpdatedClipboardFormats");
    }
#endif
    return add_listener && remove_listener && updated_formats;
}

static void test_registration(HWND hwnd, HWND other, BOOL bridge)
{
    BOOL result;
    DWORD error;
    SetLastError(0x4d9833aa);
    result = add_listener(hwnd); error = GetLastError();
    report("add", result, error);
    require(result, "first AddClipboardFormatListener");

    SetLastError(0x4d9833aa);
    result = add_listener(hwnd); error = GetLastError();
    report("duplicate add", result, error);
    require(!result && error == ERROR_INVALID_PARAMETER, "duplicate add contract");

    SetLastError(0x4d9833aa);
    result = add_listener((HWND)(ULONG_PTR)0xdead); error = GetLastError();
    report("invalid add", result, error);
    require(!result && error == ERROR_INVALID_WINDOW_HANDLE, "invalid HWND add");

    SetLastError(0x4d9833aa);
    result = remove_listener(other); error = GetLastError();
    report("unregistered remove", result, error);
    require(!result && error == ERROR_INVALID_PARAMETER, "unregistered HWND remove");

    SetLastError(0x4d9833aa);
    result = remove_listener((HWND)(ULONG_PTR)0xdead); error = GetLastError();
    report("invalid remove", result, error);
    require(!result && error == ERROR_INVALID_WINDOW_HANDLE, "invalid HWND remove");

    SetLastError(0x4d9833aa);
    result = add_listener(GetDesktopWindow()); error = GetLastError();
    report("desktop add", result, error);
    if (bridge)
        require(!result && error == ERROR_NOT_SUPPORTED,
                "foreign HWND must fail closed in bounded bridge");
    else if (result)
        require(remove_listener(GetDesktopWindow()), "desktop remove after native add");

    SetLastError(0x4d9833aa);
    result = remove_listener(hwnd); error = GetLastError();
    report("remove", result, error);
    require(result, "registered remove");
    SetLastError(0x4d9833aa);
    result = remove_listener(hwnd); error = GetLastError();
    report("duplicate remove", result, error);
    require(!result && error == ERROR_INVALID_PARAMETER, "duplicate remove contract");

    require(add_listener(other), "register second test window");
    require(DestroyWindow(other), "destroy registered test window");
    SetLastError(0x4d9833aa);
    result = remove_listener(other); error = GetLastError();
    report("destroyed remove", result, error);
    require(!result && error == ERROR_INVALID_WINDOW_HANDLE,
            "destroyed HWND must not retain registration");
}

static void test_formats(void)
{
    UINT count = 0xcccccccc, current, i;
    UINT small[2] = {0xcccccccc, 0xcccccccc};
    DWORD seq_before, seq_after, attempt;
    BOOL result;
    DWORD error;

    SetLastError(0x4d9833aa);
    result = updated_formats(large_formats, 1024, NULL); error = GetLastError();
    report("null out-count", result, error);
    require(!result && error == ERROR_NOACCESS, "null out-count contract");

    result = FALSE; error = 0;
    for (attempt = 0; attempt < 20; ++attempt) {
        current = (UINT)CountClipboardFormats();
        seq_before = GetClipboardSequenceNumber();
        count = 0xcccccccc;
        SetLastError(0x4d9833aa);
        result = updated_formats(large_formats, 1024, &count);
        error = GetLastError();
        seq_after = GetClipboardSequenceNumber();
        if (result || error != ERROR_ACCESS_DENIED) break;
        Sleep(50);
    }
    if (attempt) { say("OBSERVED: clipboard busy retries="); hex32(attempt); say("\r\n"); }
    report("all formats", result, error);
    if (current <= 1024 && seq_before == seq_after) {
        require(result, "adequate format buffer");
        if (result) require(count == current, "format count matches stable clipboard");
    } else say("OBSERVED: concurrent or oversized clipboard; count comparison skipped\r\n");
    if (!result) { require(FALSE, "adequate format buffer after bounded retries"); return; }
    for (i = 0; i < count; ++i)
        require(large_formats[i] != 0, "zero clipboard format identifier");

    if (count > 2) {
        UINT needed = 0xcccccccc;
        SetLastError(0x4d9833aa);
        result = updated_formats(small, 2, &needed); error = GetLastError();
        report("short format buffer", result, error);
        require(!result && error == ERROR_INSUFFICIENT_BUFFER && needed == count,
                "short format buffer count/error");
        require(small[0] == 0xcccccccc && small[1] == 0xcccccccc,
                "short format buffer must not be partially written");
    }
    {
        UINT needed = 0xcccccccc;
        SetLastError(0x4d9833aa);
        result = updated_formats(NULL, count, &needed); error = GetLastError();
        report("null format buffer", result, error);
        if (count) require(!result && error == ERROR_NOACCESS && needed == count,
                           "null format buffer with nonempty clipboard");
    }
}

typedef struct test_thread {
    HANDLE ready, release;
    HWND hwnd;
    BOOL opened;
} test_thread;

static DWORD WINAPI window_thread(void *parameter)
{
    test_thread *state = (test_thread *)parameter;
    state->hwnd = create_test_window();
    SetEvent(state->ready);
    while (WaitForSingleObject(state->release, 10) == WAIT_TIMEOUT)
        pump_messages(1);
    if (state->hwnd) DestroyWindow(state->hwnd);
    return 0;
}

static DWORD WINAPI busy_thread(void *parameter)
{
    test_thread *state = (test_thread *)parameter;
    state->opened = OpenClipboard(NULL);
    SetEvent(state->ready);
    WaitForSingleObject(state->release, 2000);
    if (state->opened) CloseClipboard();
    return 0;
}

static void test_other_thread(BOOL bridge)
{
    test_thread state;
    HANDLE worker;
    DWORD tid, error;
    BOOL result;
    state.hwnd = NULL; state.opened = FALSE;
    state.ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    state.release = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!state.ready || !state.release) {
        require(FALSE, "create cross-thread events");
        goto done;
    }
    worker = CreateThread(NULL, 0, window_thread, &state, 0, &tid);
    if (!worker) { require(FALSE, "create window thread"); goto done; }
    if (WaitForSingleObject(state.ready, 3000) != WAIT_OBJECT_0 || !state.hwnd)
        require(FALSE, "cross-thread window ready");
    else {
        SetLastError(0x4d9833aa);
        result = add_listener(state.hwnd); error = GetLastError();
        report("same-process cross-thread add", result, error);
        require(result, "same-process cross-thread listener");
    }
    SetEvent(state.release);
    require(WaitForSingleObject(worker, 3000) == WAIT_OBJECT_0,
            "cross-thread window destruction/exit");
    if (state.hwnd) {
        SetLastError(0x4d9833aa);
        result = remove_listener(state.hwnd); error = GetLastError();
        report("cross-thread post-exit remove", result, error);
        require(!result && error == ERROR_INVALID_WINDOW_HANDLE,
                "thread-exit window cleanup");
    }
    CloseHandle(worker);
done:
    if (state.ready) CloseHandle(state.ready);
    if (state.release) CloseHandle(state.release);
    (void)bridge;
}

static void test_busy_clipboard(BOOL bridge)
{
    test_thread state;
    HANDLE worker;
    DWORD tid, error;
    UINT count = 0xcccccccc;
    BOOL result;
    state.hwnd = NULL; state.opened = FALSE;
    state.ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    state.release = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!state.ready || !state.release) {
        require(FALSE, "create clipboard-busy events");
        goto done;
    }
    worker = CreateThread(NULL, 0, busy_thread, &state, 0, &tid);
    if (!worker) { require(FALSE, "create clipboard-busy thread"); goto done; }
    if (WaitForSingleObject(state.ready, 3000) != WAIT_OBJECT_0)
        require(FALSE, "clipboard-busy worker ready");
    else if (!state.opened)
        say("SKIP: another process held clipboard before busy probe\r\n");
    else {
        SetLastError(0x4d9833aa);
        result = updated_formats(large_formats, 1024, &count);
        error = GetLastError();
        report("other-thread clipboard lock", result, error);
        /* Both modern native USER32 and the installed Win98 fixture allow a
         * query while this process's other thread owns the clipboard. */
        require(result, "same-process other-thread clipboard format query");
    }
    SetEvent(state.release);
    require(WaitForSingleObject(worker, 3000) == WAIT_OBJECT_0,
            "clipboard-busy worker release");
    CloseHandle(worker);
done:
    if (state.ready) CloseHandle(state.ready);
    if (state.release) CloseHandle(state.release);
    (void)bridge;
}

static BOOL guest_is_win98(void)
{
    OSVERSIONINFOA version;
    version.dwOSVersionInfoSize = sizeof(version);
    return GetVersionExA(&version) && version.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS &&
           version.dwMajorVersion == 4 && version.dwMinorVersion == 10;
}

static void verify_owned_payload(HWND hwnd)
{
    HGLOBAL text, unicode;
    const char *ansi;
    const WCHAR *wide;
    if (!OpenClipboard(hwnd)) {
        require(FALSE, "open owned clipboard for content round-trip");
        return;
    }
    text = (HGLOBAL)GetClipboardData(CF_TEXT);
    require(text != NULL && GlobalSize(text) >= 4, "read back owned CF_TEXT handle");
    if (text && GlobalSize(text) >= 4) {
        ansi = (const char *)GlobalLock(text);
        require(ansi && ansi[0] == 'M' && ansi[1] == '9' &&
                ansi[2] == '8' && ansi[3] == 0, "CF_TEXT content round-trip");
        if (ansi) GlobalUnlock(text);
    }
    unicode = (HGLOBAL)GetClipboardData(CF_UNICODETEXT);
    require(unicode != NULL && GlobalSize(unicode) >= 4 * sizeof(WCHAR),
            "read back owned CF_UNICODETEXT handle");
    if (unicode && GlobalSize(unicode) >= 4 * sizeof(WCHAR)) {
        wide = (const WCHAR *)GlobalLock(unicode);
        require(wide && wide[0] == 'M' && wide[1] == '9' &&
                wide[2] == '8' && wide[3] == 0,
                "CF_UNICODETEXT content round-trip");
        if (wide) GlobalUnlock(unicode);
    }
    require(CloseClipboard(), "close owned clipboard after content round-trip");
}

static void test_guest_mutation(HWND hwnd)
{
    HGLOBAL block = NULL;
    char *p;
    WCHAR *wide;
    DWORD before, draw_before;
    UINT out_count = 0;
    UINT formats[32];
    UINT i, found_text = 0, found_unicode = 0;
    UINT short_count = 0xcccccccc, small = 0xcccccccc;
    BOOL short_result;
    BOOL opened = FALSE, listener_added = FALSE, viewer_added = FALSE, changed = FALSE;
    DWORD failures_before = failures;
    if (!guest_is_win98()) {
        say("SKIP: mutation requires actual Windows 98 guest\r\n");
        return;
    }
    if (!OpenClipboard(hwnd)) {
        say("SKIP: clipboard unavailable for owned mutation\r\n");
        return;
    }
    opened = TRUE;
    if (CountClipboardFormats() != 0) {
        say("SKIP: preexisting clipboard payload; no destructive mutation\r\n");
        goto cleanup;
    }
    /* The empty original state is an exact, restorable snapshot. */
    if (!CloseClipboard()) { require(FALSE, "close preflight clipboard"); goto cleanup; }
    opened = FALSE;
    listener_added = add_listener(hwnd);
    require(listener_added, "add listener for guest update");
    if (!listener_added) goto cleanup;
    SetLastError(ERROR_SUCCESS);
    next_viewer = SetClipboardViewer(hwnd);
    viewer_added = next_viewer != NULL || GetLastError() == ERROR_SUCCESS;
    require(viewer_added, "insert owned viewer in Win98 chain");
    if (!viewer_added) goto cleanup;
    before = (DWORD)update_count;
    pump_messages(100);
    require(update_count == (LONG)before,
            "viewer registration must not synthesize a clipboard update");
    InterlockedExchange(&event_serial, 0);
    InterlockedExchange(&first_update_event, 0);
    InterlockedExchange(&first_draw_event, 0);
    before = (DWORD)update_count;
    draw_before = (DWORD)draw_count;
    if (!OpenClipboard(hwnd)) {
        require(FALSE, "open guest clipboard for mutation");
        goto cleanup;
    }
    opened = TRUE;
    if (!EmptyClipboard()) {
        require(FALSE, "empty clipboard during owned mutation");
        goto cleanup;
    }
    changed = TRUE;
    block = GlobalAlloc(GMEM_MOVEABLE, 8);
    if (!block) { require(FALSE, "allocate owned CF_TEXT"); goto cleanup; }
    p = (char *)GlobalLock(block);
    if (!p) { require(FALSE, "lock owned CF_TEXT"); goto cleanup; }
    p[0] = 'M'; p[1] = '9'; p[2] = '8'; p[3] = 0;
    GlobalUnlock(block);
    if (!SetClipboardData(CF_TEXT, block)) {
        require(FALSE, "set owned CF_TEXT"); goto cleanup;
    }
    block = NULL; /* clipboard owns it */
    block = GlobalAlloc(GMEM_MOVEABLE, 8);
    if (!block) { require(FALSE, "allocate owned CF_UNICODETEXT"); goto cleanup; }
    wide = (WCHAR *)GlobalLock(block);
    if (!wide) { require(FALSE, "lock owned CF_UNICODETEXT"); goto cleanup; }
    wide[0] = 'M'; wide[1] = '9'; wide[2] = '8'; wide[3] = 0;
    GlobalUnlock(block);
    if (!SetClipboardData(CF_UNICODETEXT, block)) {
        require(FALSE, "set owned CF_UNICODETEXT"); goto cleanup;
    }
    block = NULL;
    if (!updated_formats(formats, 32, &out_count))
        require(FALSE, "query formats while clipboard open");
    else {
        require(out_count >= 2, "two explicit clipboard formats");
        for (i = 0; i < out_count && i < 32; ++i) {
            found_text |= formats[i] == CF_TEXT;
            found_unicode |= formats[i] == CF_UNICODETEXT;
        }
        require(found_text && found_unicode, "all explicit format identifiers");
    }
    SetLastError(0x4d9833aa);
    short_result = updated_formats(&small, 1, &short_count);
    require(!short_result && GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
            short_count >= 2 && small == 0xcccccccc,
            "owned short format buffer fails without partial copy");
    if (!CloseClipboard()) { require(FALSE, "close owned clipboard update"); goto cleanup; }
    opened = FALSE;
    verify_owned_payload(hwnd);
    say("OBSERVED: update count before pump="); hex32((DWORD)update_count);
    say(" viewer draw count before pump="); hex32((DWORD)draw_count); say("\r\n");
    pump_messages(200);
    report("guest update count", update_count > (LONG)before, GetLastError());
    require(update_count > (LONG)before && bad_update_params == 0,
            "posted WM_CLIPBOARDUPDATE with zero parameters");
    require(draw_count > (LONG)draw_before,
            "legacy WM_DRAWCLIPBOARD viewer received update");
    say("OBSERVED: first WM_CLIPBOARDUPDATE order=");
    hex32((DWORD)first_update_event);
    say(" first WM_DRAWCLIPBOARD order=");
    hex32((DWORD)first_draw_event); say("\r\n");
    out_count = 0;
    {
        UINT native_count_before = (UINT)CountClipboardFormats();
        DWORD sequence_before = GetClipboardSequenceNumber();
        BOOL formats_ok = updated_formats(formats, 32, &out_count);
        DWORD formats_error = GetLastError();
        UINT native_count_after = (UINT)CountClipboardFormats();
        DWORD sequence_after = GetClipboardSequenceNumber();
        UINT native_enum_count = 0, native_id = 0;
        say("OBSERVED: post-close Count before="); hex32(native_count_before);
        say(" GetUpdated count="); hex32(out_count);
        say(" Count after="); hex32(native_count_after);
        say(" sequence before="); hex32(sequence_before);
        say(" sequence after="); hex32(sequence_after);
        say(" GetUpdated result="); hex32(formats_ok);
        say(" error="); hex32(formats_error); say("\r\n");
        if (formats_ok) {
            for (i = 0; i < out_count && i < 32; ++i) {
                say("OBSERVED: GetUpdated format="); hex32(formats[i]); say("\r\n");
            }
        }
        if (OpenClipboard(hwnd)) {
            SetLastError(ERROR_SUCCESS);
            while ((native_id = EnumClipboardFormats(native_id)) != 0 && native_enum_count < 32) {
                ++native_enum_count;
                say("OBSERVED: native Enum format="); hex32(native_id); say("\r\n");
                SetLastError(ERROR_SUCCESS);
            }
            say("OBSERVED: native Enum count="); hex32(native_enum_count);
            say(" end error="); hex32(GetLastError()); say("\r\n");
            CloseClipboard();
        } else {
            say("OBSERVED: native Enum open failed error="); hex32(GetLastError());
            say("\r\n");
        }
        if (!formats_ok)
            require(FALSE, "query all formats after clipboard close");
        else require(out_count == native_count_before &&
                     native_count_before == native_count_after &&
                     sequence_before == sequence_after,
                     "post-close synthesized format count");
    }
    {
        DWORD updates_before = (DWORD)update_count;
        DWORD draws_before = (DWORD)draw_count;
        const UINT changes = 6;
        UINT change;
        /* Keep the caller's queue unpumped across all CloseClipboard calls.
         * This exercises queued viewer notifications for rapid changes. */
        for (change = 0; change < changes; ++change) {
            if (!OpenClipboard(hwnd)) {
                require(FALSE, "open owned clipboard for notification burst");
                goto cleanup;
            }
            opened = TRUE;
            if (!EmptyClipboard()) {
                require(FALSE, "empty owned clipboard during notification burst");
                goto cleanup;
            }
            block = GlobalAlloc(GMEM_MOVEABLE, 4);
            if (!block) {
                require(FALSE, "allocate owned burst payload");
                goto cleanup;
            }
            p = (char *)GlobalLock(block);
            if (!p) {
                require(FALSE, "lock owned burst payload");
                goto cleanup;
            }
            p[0] = 'M'; p[1] = '9'; p[2] = '8'; p[3] = 0;
            GlobalUnlock(block);
            if (!SetClipboardData(CF_TEXT, block)) {
                require(FALSE, "set owned burst payload");
                goto cleanup;
            }
            block = NULL;
            if (!CloseClipboard()) {
                require(FALSE, "close owned clipboard burst update");
                goto cleanup;
            }
            opened = FALSE;
        }
        pump_messages(500);
        say("OBSERVED: queued burst WM_CLIPBOARDUPDATE delta=");
        hex32((DWORD)update_count - updates_before);
        say(" WM_DRAWCLIPBOARD delta=");
        hex32((DWORD)draw_count - draws_before); say("\r\n");
        require((DWORD)update_count - updates_before >= changes,
                "every owned burst change posts WM_CLIPBOARDUPDATE");
        require((DWORD)draw_count - draws_before >= changes,
                "legacy viewer sees every owned burst change");
    }

cleanup:
    if (opened) CloseClipboard();
    if (block) GlobalFree(block);
    if (viewer_added) ChangeClipboardChain(hwnd, next_viewer);
    next_viewer = NULL;
    if (listener_added) remove_listener(hwnd);
    if (changed) {
        if (!OpenClipboard(hwnd)) require(FALSE, "reopen clipboard to restore empty state");
        else {
            if (!EmptyClipboard()) require(FALSE, "restore original empty clipboard");
            if (!CloseClipboard()) require(FALSE, "close restored empty clipboard");
        }
    }
    if (changed && failures == failures_before)
        say("PASS: owned guest clipboard mutation, notifications, formats, and empty restore\r\n");
}

void __cdecl mainCRTStartup(void)
{
    static WNDCLASSA wc;
    HWND hwnd, other;
    BOOL shipping = has_option(GetCommandLineA(), "--shipping");
    BOOL bridge = has_option(GetCommandLineA(), "--bridge") || shipping;
    BOOL bounded_provider = bridge || has_option(GetCommandLineA(), "--bounded-provider");
    BOOL require_api = has_option(GetCommandLineA(), "--require-api") || bridge;
    BOOL mutate = has_option(GetCommandLineA(), "--guest-mutate");
    HINSTANCE instance = GetModuleHandleA(NULL);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = instance;
    wc.lpszClassName = "M98ClipboardContract";
    if (!RegisterClassA(&wc)) { say("FAIL: RegisterClassA\r\n"); ExitProcess(1); }
    hwnd = create_test_window();
    other = create_test_window();
    if (!hwnd || !other) { say("FAIL: CreateWindowExA\r\n"); ExitProcess(1); }
    if (!load_apis(bridge, shipping)) {
        say(require_api ? "FAIL: clipboard API trio unavailable\r\n" :
                          "SKIP: original USER32 clipboard API trio absent\r\n");
        ExitProcess(require_api ? 1 : 0);
    }
    test_registration(hwnd, other, bounded_provider);
    test_other_thread(bridge);
    test_formats();
    test_busy_clipboard(bridge);
    if (mutate) test_guest_mutation(hwnd);
    DestroyWindow(hwnd);
    if (provider && bridge) FreeLibrary(provider);
    if (failures) { say("FAIL: clipboard contract mismatches\r\n"); ExitProcess(1); }
    say("PASS: clipboard listener and format contract probe\r\n");
    ExitProcess(0);
}
