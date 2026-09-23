/* Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 * New Win98 backend, reviewed against Wine df15af3652511150490934682202d45af892f887:
 * dlls/win32u/clipboard.c, server/clipboard.c, dlls/user32/tests/clipboard.c;
 * ReactOS 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8:
 * win32ss/user/{user32/windows,ntuser}/clipboard.c. ReactOS's three modern
 * entries are unimplemented. No NT server/PEB code or upstream body is copied.
 * Full inventory, differences and lifetime boundaries: docs/CLIPBOARD_PORT.md.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "m98_clipboard.h"

#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031d
#endif
#define M98_CLIP_ADD 1
#define M98_CLIP_REMOVE 2
#define M98_CLIP_SWEEP (WM_APP + 37)
#define M98_CLIP_TIMER 1

typedef struct m98_listener {
    struct m98_listener *next;
    HWND window;
    DWORD thread_id;
    HANDLE cookie;
} M98_LISTENER;

static const char m98_owner_property[] = "Shizuku.Clipboard.Owner.v1";
static const char m98_cookie_property[] = "Shizuku.Clipboard.Cookie.v1";
static const char m98_command_name[] = "Shizuku.Clipboard.Command.v1";
static HINSTANCE m98_instance;
static volatile LONG m98_start_state;
static DWORD m98_start_error;
static HANDLE m98_ready_event;
static HWND m98_worker_window, m98_next_viewer;
static UINT m98_command_message;
static BOOL m98_in_chain, m98_ignore_initial_draw, m98_detaching;
static DWORD m98_cookie_serial;
static M98_LISTENER *m98_listeners;

void m98_clipboard_attach(HINSTANCE instance)
{
    m98_instance = instance;
}

static BOOL m98_error(DWORD error)
{
    SetLastError(error);
    return FALSE;
}

static DWORD m98_validate_window(HWND window)
{
    DWORD process = 0;
    if (!window || !IsWindow(window) || !GetWindowThreadProcessId(window, &process))
        return ERROR_INVALID_WINDOW_HANDLE;
    /* Wine's server can retain a foreign HWND beyond the registering process.
     * A Win98 process-local worker cannot supply that lifetime contract. */
    if (process != GetCurrentProcessId()) return ERROR_NOT_SUPPORTED;
    return ERROR_SUCCESS;
}

static BOOL m98_listener_alive(const M98_LISTENER *listener)
{
    DWORD process = 0;
    DWORD thread = GetWindowThreadProcessId(listener->window, &process);
    return thread && thread == listener->thread_id &&
           process == GetCurrentProcessId() &&
           GetPropA(listener->window, m98_owner_property) == (HANDLE)m98_worker_window &&
           GetPropA(listener->window, m98_cookie_property) == listener->cookie;
}

static void m98_dispose_listener(M98_LISTENER *listener)
{
    /* Window destruction removes its properties. An HWND reincarnation must
     * not lose properties installed for a newer registration. */
    if (GetPropA(listener->window, m98_owner_property) == (HANDLE)m98_worker_window &&
        GetPropA(listener->window, m98_cookie_property) == listener->cookie) {
        RemovePropA(listener->window, m98_cookie_property);
        RemovePropA(listener->window, m98_owner_property);
    }
    HeapFree(GetProcessHeap(), 0, listener);
}

static void m98_prune_listeners(void)
{
    M98_LISTENER **link = &m98_listeners;
    while (*link) {
        M98_LISTENER *listener = *link;
        if (m98_listener_alive(listener)) link = &listener->next;
        else {
            *link = listener->next;
            m98_dispose_listener(listener);
        }
    }
}

static void m98_leave_chain(void)
{
    HWND next;
    if (!m98_in_chain) return;
    next = m98_next_viewer;
    m98_in_chain = FALSE;
    m98_detaching = TRUE;
    /* This legacy API may synchronously contact another viewer. Invoke it
     * only from the worker's posted sweep, after any caller's RPC returned. */
    ChangeClipboardChain(m98_worker_window, IsWindow(next) ? next : NULL);
    m98_next_viewer = NULL;
    m98_detaching = FALSE;
}

static DWORD m98_join_chain(void)
{
    HWND previous;
    DWORD error;
    if (m98_in_chain) return ERROR_SUCCESS;
    m98_next_viewer = GetClipboardViewer();
    m98_ignore_initial_draw = TRUE;
    SetLastError(ERROR_SUCCESS);
    previous = SetClipboardViewer(m98_worker_window);
    error = GetLastError();
    /* Another process can insert a viewer immediately after this call, so
     * being the current chain head is not a valid success test. NULL is also
     * a valid previous viewer for an initially empty chain. */
    if (!previous && error) {
        m98_ignore_initial_draw = FALSE;
        m98_next_viewer = NULL;
        return error;
    }
    m98_next_viewer = previous;
    m98_in_chain = TRUE;
    return ERROR_SUCCESS;
}

static DWORD m98_add_listener(HWND window)
{
    M98_LISTENER *listener;
    DWORD error = m98_validate_window(window);
    HANDLE owner;
    if (error) return error;
    /* ChangeClipboardChain can dispatch a reentrant sent message. Do not
     * reinsert this window while its previous unlink is still in progress. */
    if (m98_detaching) return ERROR_BUSY;
    m98_prune_listeners();
    owner = GetPropA(window, m98_owner_property);
    if (owner && IsWindow((HWND)owner)) return ERROR_INVALID_PARAMETER;
    listener = (M98_LISTENER *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                         sizeof(*listener));
    if (!listener) return ERROR_NOT_ENOUGH_MEMORY;
    listener->window = window;
    listener->thread_id = GetWindowThreadProcessId(window, NULL);
    if (++m98_cookie_serial == 0) ++m98_cookie_serial;
    listener->cookie = (HANDLE)(ULONG_PTR)m98_cookie_serial;
    error = m98_join_chain();
    if (error) {
        HeapFree(GetProcessHeap(), 0, listener);
        return error;
    }
    if (!SetPropA(window, m98_owner_property, (HANDLE)m98_worker_window)) {
        error = GetLastError();
        HeapFree(GetProcessHeap(), 0, listener);
        PostMessageA(m98_worker_window, M98_CLIP_SWEEP, 0, 0);
        return error ? error : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!SetPropA(window, m98_cookie_property, listener->cookie)) {
        error = GetLastError();
        RemovePropA(window, m98_owner_property);
        HeapFree(GetProcessHeap(), 0, listener);
        PostMessageA(m98_worker_window, M98_CLIP_SWEEP, 0, 0);
        return error ? error : ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!m98_listener_alive(listener)) {
        m98_dispose_listener(listener);
        PostMessageA(m98_worker_window, M98_CLIP_SWEEP, 0, 0);
        return ERROR_INVALID_WINDOW_HANDLE;
    }
    listener->next = m98_listeners;
    m98_listeners = listener;
    return ERROR_SUCCESS;
}

static DWORD m98_remove_listener(HWND window)
{
    M98_LISTENER **link;
    DWORD error = m98_validate_window(window);
    if (error) return error;
    m98_prune_listeners();
    for (link = &m98_listeners; *link; link = &(*link)->next) {
        if ((*link)->window == window) {
            M98_LISTENER *listener = *link;
            *link = listener->next;
            m98_dispose_listener(listener);
            PostMessageA(m98_worker_window, M98_CLIP_SWEEP, 0, 0);
            return ERROR_SUCCESS;
        }
    }
    return ERROR_INVALID_PARAMETER;
}

static void m98_notify_listeners(void)
{
    M98_LISTENER *listener;
    m98_prune_listeners();
    /* SetClipboardViewer itself generates one initial DRAW notification.
     * Suppress that one, not all messages with an equal current sequence:
     * queued DRAWs for distinct changes may all observe the latest sequence
     * when this worker finally receives them. Each subsequent DRAW must post
     * its own update, as Wine server close_clipboard/notify_listeners does. */
    if (m98_ignore_initial_draw) {
        m98_ignore_initial_draw = FALSE;
        return;
    }
    for (listener = m98_listeners; listener; listener = listener->next)
        if (m98_listener_alive(listener))
            PostMessageA(listener->window, WM_CLIPBOARDUPDATE, 0, 0);
}

static LRESULT CALLBACK m98_clipboard_window(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam)
{
    if (m98_command_message && message == m98_command_message) {
        if (wparam == M98_CLIP_ADD) return m98_add_listener((HWND)lparam);
        if (wparam == M98_CLIP_REMOVE) return m98_remove_listener((HWND)lparam);
        return ERROR_INVALID_PARAMETER;
    }
    switch (message) {
    case WM_DRAWCLIPBOARD:
        m98_notify_listeners();
        /* SendNotifyMessage keeps the native sent-message chain contract
         * without waiting for an application thread blocked in Add/Remove.
         * No application WndProc or caller queue is pumped by this bridge. */
        if (m98_next_viewer && m98_next_viewer != window)
            SendNotifyMessageA(m98_next_viewer, message, wparam, lparam);
        return 0;
    case WM_CHANGECBCHAIN:
        if ((HWND)wparam == m98_next_viewer) m98_next_viewer = (HWND)lparam;
        else if (m98_next_viewer && m98_next_viewer != window)
            SendNotifyMessageA(m98_next_viewer, message, wparam, lparam);
        return 0;
    case WM_TIMER:
    case M98_CLIP_SWEEP:
        m98_prune_listeners();
        if (!m98_listeners) m98_leave_chain();
        return 0;
    case WM_CLOSE:
        return 0; /* Lifetime belongs to the pinned process backend. */
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static DWORD WINAPI m98_clipboard_worker(LPVOID parameter)
{
    WNDCLASSA klass = { 0 };
    HMODULE pin = (HMODULE)parameter;
    BOOL class_registered = FALSE;
    char name[] = "M98ClipboardWorker00000000";
    static const char digits[] = "0123456789abcdef";
    UINT i;
    MSG message;
    for (i = 0; i < 8; ++i)
        name[18 + i] = digits[((DWORD)(ULONG_PTR)m98_instance >> ((7 - i) * 4)) & 15];
    klass.hInstance = m98_instance;
    klass.lpfnWndProc = m98_clipboard_window;
    klass.lpszClassName = name;
    if (!RegisterClassA(&klass)) goto failed;
    class_registered = TRUE;
    m98_worker_window = CreateWindowExA(0, name, "", WS_POPUP, 0, 0, 0, 0,
                                        NULL, NULL, m98_instance, NULL);
    if (!m98_worker_window) goto failed;
    if (!SetTimer(m98_worker_window, M98_CLIP_TIMER, 250, NULL)) goto failed;
    InterlockedExchange(&m98_start_state, 2);
    SetEvent(m98_ready_event);
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    m98_start_error = ERROR_OPERATION_ABORTED;
    InterlockedExchange(&m98_start_state, -1);
    return 0;
failed:
    m98_start_error = GetLastError();
    if (!m98_start_error) m98_start_error = ERROR_GEN_FAILURE;
    if (m98_worker_window) DestroyWindow(m98_worker_window);
    m98_worker_window = NULL;
    if (class_registered) UnregisterClassA(name, m98_instance);
    InterlockedExchange(&m98_start_state, -1);
    SetEvent(m98_ready_event);
    /* Initialization never established a live backend. Release its extra
     * module reference atomically with thread exit; returning after a plain
     * FreeLibrary could execute unmapped worker code. */
    FreeLibraryAndExitThread(pin, m98_start_error);
    return 0;
}

static BOOL m98_ready(void)
{
    LONG state = InterlockedCompareExchange(&m98_start_state, 1, 0);
    if (!state) {
        char path[MAX_PATH];
        HMODULE pin;
        HANDLE thread;
        DWORD thread_id, length;
        m98_command_message = RegisterWindowMessageA(m98_command_name);
        if (!m98_command_message) goto failed;
        m98_ready_event = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!m98_ready_event) goto failed;
        length = GetModuleFileNameA(m98_instance, path, sizeof(path));
        if (!length || length >= sizeof(path)) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            goto failed;
        }
        /* Win98 has no GetModuleHandleEx(PIN). One normal LoadLibrary reference
         * keeps worker code and WndProc alive until process exit. Never create
         * or wait for the worker from DllMain. */
        pin = LoadLibraryA(path);
        if (!pin) goto failed;
        thread = CreateThread(NULL, 0, m98_clipboard_worker, pin, 0, &thread_id);
        if (!thread) {
            DWORD error = GetLastError();
            FreeLibrary(pin);
            SetLastError(error);
            goto failed;
        }
        CloseHandle(thread);
    }
    while (m98_start_state == 1) {
        if (m98_ready_event) WaitForSingleObject(m98_ready_event, 100);
        else Sleep(0);
    }
    if (m98_start_state == 2) return TRUE;
    return m98_error(m98_start_error);
failed:
    m98_start_error = GetLastError();
    if (!m98_start_error) m98_start_error = ERROR_GEN_FAILURE;
    InterlockedExchange(&m98_start_state, -1);
    return m98_error(m98_start_error);
}

static BOOL m98_listener_call(HWND window, UINT operation)
{
    DWORD saved_error = GetLastError(), error = m98_validate_window(window);
    DWORD_PTR result;
    HWND broker;
    if (error) return m98_error(error);
    if (!m98_ready()) return FALSE;
    broker = m98_worker_window;
    if (operation == M98_CLIP_REMOVE) {
        HWND owner = (HWND)GetPropA(window, m98_owner_property);
        DWORD process = 0;
        if (!owner || !GetWindowThreadProcessId(owner, &process) ||
            process != GetCurrentProcessId()) return m98_error(ERROR_INVALID_PARAMETER);
        broker = owner;
    }
    SetLastError(ERROR_SUCCESS);
    if (!SendMessageTimeoutA(broker, m98_command_message, operation, (LPARAM)window,
                             SMTO_BLOCK | SMTO_ABORTIFHUNG, 5000, &result)) {
        error = GetLastError();
        return m98_error(error ? error : ERROR_TIMEOUT);
    }
    if (result) return m98_error((DWORD)result);
    SetLastError(saved_error);
    return TRUE;
}

BOOL WINAPI m98_AddClipboardFormatListener(HWND window)
{
    return m98_listener_call(window, M98_CLIP_ADD);
}

BOOL WINAPI m98_RemoveClipboardFormatListener(HWND window)
{
    return m98_listener_call(window, M98_CLIP_REMOVE);
}

BOOL WINAPI m98_GetUpdatedClipboardFormats(PUINT formats, UINT capacity,
                                           PUINT count_out)
{
    UINT *snapshot = NULL;
    UINT count = 0, allocated = 0, format;
    DWORD saved_error = GetLastError(), error = ERROR_SUCCESS;
    BOOL opened = FALSE;
    if (!count_out || IsBadWritePtr(count_out, sizeof(*count_out)))
        return m98_error(ERROR_NOACCESS);
    /* Enum first distinguishes a clipboard already opened by this caller;
     * opening it again and closing it would steal the caller's open lifetime.
     * Wine's server avoids this temporary native lock, which Win98 lacks. */
    SetLastError(ERROR_SUCCESS);
    format = EnumClipboardFormats(0);
    error = format ? ERROR_SUCCESS : GetLastError();
    /* Original Win98 EnumClipboardFormats(0), with no open clipboard,
     * returns zero WITHOUT setting LastError. The guest negative control
     * recorded CountClipboardFormats()==4, sequence unchanged, but Enum's
     * apparent empty result. A nonempty native count disambiguates this case.
     * Already-open nonempty callers returned a first format above; already-
     * open empty callers have count zero and are not reopened/closed here. */
    if (error == ERROR_CLIPBOARD_NOT_OPEN || error == ERROR_ACCESS_DENIED ||
        (!format && !error && CountClipboardFormats() > 0)) {
        if (!OpenClipboard(NULL)) return FALSE;
        opened = TRUE;
        SetLastError(ERROR_SUCCESS);
        format = EnumClipboardFormats(0);
        error = format ? ERROR_SUCCESS : GetLastError();
    }
    while (format && !error) {
        if (count == allocated) {
            UINT next = allocated ? allocated * 2 : 16;
            UINT *replacement;
            if (next < allocated || next > (UINT)-1 / sizeof(UINT)) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            replacement = snapshot ?
                (UINT *)HeapReAlloc(GetProcessHeap(), 0, snapshot, next * sizeof(UINT)) :
                (UINT *)HeapAlloc(GetProcessHeap(), 0, next * sizeof(UINT));
            if (!replacement) { error = ERROR_NOT_ENOUGH_MEMORY; break; }
            snapshot = replacement;
            allocated = next;
        }
        snapshot[count++] = format;
        SetLastError(ERROR_SUCCESS);
        format = EnumClipboardFormats(format);
        error = format ? ERROR_SUCCESS : GetLastError();
    }
    if (opened) {
        if (!CloseClipboard() && !error) error = GetLastError();
    }
    if (!error) {
        *count_out = count;
        if (count && !formats) error = ERROR_NOACCESS;
        else if (capacity < count) error = ERROR_INSUFFICIENT_BUFFER;
        else if (count && IsBadWritePtr(formats, count * sizeof(UINT))) error = ERROR_NOACCESS;
        else for (UINT i = 0; i < count; ++i) formats[i] = snapshot[i];
    }
    if (snapshot) HeapFree(GetProcessHeap(), 0, snapshot);
    if (error) return m98_error(error);
    SetLastError(saved_error);
    return TRUE;
}
