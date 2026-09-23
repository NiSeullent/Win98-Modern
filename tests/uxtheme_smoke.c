/* Host/guest smoke: the no-theme path must return failures and keep outputs. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#define NTDDI_VERSION 0x06000000
#include <windows.h>
#include <uxtheme.h>

typedef HTHEME (WINAPI *open_theme_fn)(HWND, LPCWSTR);
typedef HRESULT (WINAPI *close_theme_fn)(HTHEME);
typedef HRESULT (WINAPI *draw_text_ex_fn)(HTHEME,HDC,int,int,LPCWSTR,int,DWORD,RECT *,const DTTOPTS *);
typedef HRESULT (WINAPI *get_theme_color_fn)(HTHEME,int,int,int,COLORREF *);
typedef HRESULT (WINAPI *get_content_rect_fn)(HTHEME,HDC,int,int,const RECT *,RECT *);
typedef HRESULT (WINAPI *draw_parent_fn)(HWND,HDC,RECT *);
typedef HANIMATIONBUFFER (WINAPI *begin_animation_fn)(HWND,HDC,const RECT *,BP_BUFFERFORMAT,BP_PAINTPARAMS *,BP_ANIMATIONPARAMS *,HDC *,HDC *);
typedef BOOL (WINAPI *render_animation_fn)(HWND,HDC);
typedef HRESULT (WINAPI *end_animation_fn)(HANIMATIONBUFFER,BOOL);
typedef HRESULT (WINAPI *stop_animation_fn)(HWND);
typedef BOOL (WINAPI *is_theme_active_fn)(void);
typedef HRESULT (WINAPI *set_window_theme_fn)(HWND,LPCWSTR,LPCWSTR);
typedef HRESULT (WINAPI *dialog_texture_fn)(HWND,DWORD);

static int erase_calls, print_calls;

static void report(const char *message)
{
    DWORD length = 0, written;
    while (message[length]) length++;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, NULL);
}

static void report_hex(const char *label, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char buffer[9];
    int index;
    for (index = 7; index >= 0; index--) {
        buffer[index] = digits[value & 15];
        value >>= 4;
    }
    buffer[8] = 0;
    report(label); report(buffer); report("\r\n");
}

static void fail(const char *message)
{
    report("FAIL: "); report(message); report("\r\n");
    ExitProcess(1);
}

static LRESULT CALLBACK parent_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_ERASEBKGND) {
        RECT area;
        HBRUSH brush = CreateSolidBrush(RGB(193, 49, 12));
        erase_calls++;
        GetClientRect(window, &area);
        FillRect((HDC)wparam, &area, brush);
        DeleteObject(brush);
        return 1;
    }
    if (message == WM_PRINTCLIENT) {
        print_calls++;
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    HMODULE module = LoadLibraryA("m98uxtheme.dll");
    open_theme_fn open_theme;
    close_theme_fn close_theme;
    draw_text_ex_fn draw_text;
    get_theme_color_fn get_color;
    get_content_rect_fn get_rect;
    draw_parent_fn draw_parent;
    begin_animation_fn begin_animation;
    render_animation_fn render_animation;
    end_animation_fn end_animation;
    stop_animation_fn stop_animation;
    is_theme_active_fn is_theme_active;
    set_window_theme_fn set_window_theme;
    dialog_texture_fn dialog_texture;
    RECT original = {1, 2, 21, 22}, changed = {9, 9, 9, 9};
    RECT bitmap_area = {0, 0, 24, 24};
    COLORREF color = RGB(1, 2, 3);
    HDC from = (HDC)1, to = (HDC)2, memory_dc, screen_dc;
    HBITMAP bitmap, previous;
    HBRUSH background;
    COLORREF initial, painted;
    WNDCLASSA window_class = {0};
    HWND parent, child;

    if (!module) fail("load app-local theme bridge by unique test filename");
    open_theme = (open_theme_fn)GetProcAddress(module, "OpenThemeData");
    close_theme = (close_theme_fn)GetProcAddress(module, "CloseThemeData");
    draw_text = (draw_text_ex_fn)GetProcAddress(module, "DrawThemeTextEx");
    get_color = (get_theme_color_fn)GetProcAddress(module, "GetThemeColor");
    get_rect = (get_content_rect_fn)GetProcAddress(module, "GetThemeBackgroundContentRect");
    draw_parent = (draw_parent_fn)GetProcAddress(module, "DrawThemeParentBackground");
    begin_animation = (begin_animation_fn)GetProcAddress(module, "BeginBufferedAnimation");
    render_animation = (render_animation_fn)GetProcAddress(module, "BufferedPaintRenderAnimation");
    end_animation = (end_animation_fn)GetProcAddress(module, "EndBufferedAnimation");
    stop_animation = (stop_animation_fn)GetProcAddress(module, "BufferedPaintStopAllAnimations");
    is_theme_active = (is_theme_active_fn)GetProcAddress(module, "IsThemeActive");
    set_window_theme = (set_window_theme_fn)GetProcAddress(module, "SetWindowTheme");
    dialog_texture = (dialog_texture_fn)GetProcAddress(module, "EnableThemeDialogTexture");
    if (!open_theme || !close_theme || !draw_text || !get_color || !get_rect ||
        !draw_parent || !begin_animation || !render_animation ||
        !end_animation || !stop_animation || !is_theme_active ||
        !set_window_theme || !dialog_texture ||
        GetProcAddress(module, "DrawThemeTextEx@36"))
        fail("exact undecorated theme exports");
    if (is_theme_active() || open_theme(NULL, L"BUTTON") || close_theme(NULL) != E_HANDLE)
        fail("no theme handles on Windows 98");
    if (draw_text(NULL, NULL, 0, 0, L"test", -1, 0, &original, NULL) != E_HANDLE ||
        original.left != 1 || original.top != 2 || original.right != 21 || original.bottom != 22)
        fail("DrawThemeTextEx rejects missing theme without drawing");
    if (get_color(NULL, 0, 0, 0, &color) != E_HANDLE || color != RGB(1, 2, 3))
        fail("GetThemeColor leaves output untouched on absent theme");
    if (get_rect(NULL, NULL, 0, 0, &original, &changed) != E_HANDLE ||
        changed.left != 9 || changed.right != 9)
        fail("GetThemeBackgroundContentRect leaves output untouched");
    if (begin_animation(NULL, NULL, &original, BPBF_COMPATIBLEBITMAP,
                        NULL, NULL, &from, &to) || from || to ||
        render_animation(NULL, NULL) || end_animation(NULL, FALSE) != E_INVALIDARG)
        fail("buffered animation reports fallback accurately");
    if (draw_parent(NULL, NULL, NULL) != E_HANDLE ||
        stop_animation(NULL) != E_HANDLE)
        fail("invalid parent or animation window");

    window_class.lpfnWndProc = parent_proc;
    window_class.hInstance = GetModuleHandleA(NULL);
    window_class.lpszClassName = "M98ThemeSmokeParent";
    if (!RegisterClassA(&window_class)) fail("register test parent");
    parent = CreateWindowExA(0, window_class.lpszClassName, "parent",
                             WS_OVERLAPPEDWINDOW, 20, 20, 80, 80,
                             NULL, NULL, window_class.hInstance, NULL);
    child = CreateWindowExA(0, "STATIC", "child", WS_CHILD,
                            6, 7, 24, 24, parent, NULL,
                            window_class.hInstance, NULL);
    if (!parent || !child) fail("create parent and child windows");
    screen_dc = GetDC(NULL);
    memory_dc = CreateCompatibleDC(screen_dc);
    bitmap = CreateCompatibleBitmap(screen_dc, 24, 24);
    previous = (HBITMAP)SelectObject(memory_dc, bitmap);
    if (!screen_dc || !memory_dc || !bitmap || !previous)
        fail("create test bitmap and DC");
    background = CreateSolidBrush(RGB(0, 0, 0));
    if (!background || !FillRect(memory_dc, &bitmap_area, background))
        fail("initialize paint target");
    DeleteObject(background);
    initial = GetPixel(memory_dc, 1, 1);
    if (initial == CLR_INVALID) fail("read initial paint target");
    if (draw_parent(child, memory_dc, NULL) != S_OK)
        fail("DrawThemeParentBackground returns success after parent paint");
    painted = GetPixel(memory_dc, 1, 1);
    if (erase_calls < 1 || print_calls < 1 || painted == CLR_INVALID ||
        painted == initial) {
        report_hex("parent erase calls=", (DWORD)erase_calls);
        report_hex("parent print calls=", (DWORD)print_calls);
        report_hex("initial pixel=", initial);
        report_hex("painted pixel=", painted);
        fail("DrawThemeParentBackground asks real parent to paint");
    }
    if (stop_animation(child) != S_OK)
        fail("stopping zero active animations");
    if (set_window_theme(child, NULL, NULL) != S_OK ||
        set_window_theme(child, L"Explorer", L"Button") != E_NOTIMPL ||
        dialog_texture(child, ETDT_DISABLE) != S_OK ||
        dialog_texture(child, ETDT_ENABLE) != E_NOTIMPL)
        fail("theme requests expose absent service without fake success");
    SelectObject(memory_dc, previous);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);
    DestroyWindow(parent);
    FreeLibrary(module);
    report("PASS: no-theme error paths, animation fallback and real parent paint\r\n");
    ExitProcess(0);
}
