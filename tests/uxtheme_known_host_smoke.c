/* Host-only 32-bit smoke for the KernelEx UXTHEME KnownDLL replacement.
 * This loads by path to verify that both original and added exports execute.
 * The real Windows 98 import/loader behavior still needs guest validation.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>

static void say(const char *message)
{
    DWORD written;
    DWORD length = 0;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    while (message[length]) ++length;
    WriteFile(output, message, length, &written, NULL);
}

static BOOL contains(const char *text, const char *part)
{
    const char *candidate;
    const char *needle;
    for (; *text; ++text) {
        candidate = text;
        needle = part;
        while (*candidate && *needle && *candidate == *needle) {
            ++candidate;
            ++needle;
        }
        if (!*needle) return TRUE;
    }
    return FALSE;
}

void __cdecl mainCRTStartup(void)
{
    HMODULE module;
    char loaded[MAX_PATH];
    BOOL (WINAPI *is_theme_active)(void);
    HTHEME (WINAPI *open_theme)(HWND, LPCWSTR);
    HRESULT (WINAPI *draw_parent_background)(HWND, HDC, RECT *);
    COLORREF (WINAPI *get_theme_sys_color)(HTHEME, int);
    HRESULT (WINAPI *draw_theme_text_ex)(HTHEME, HDC, int, int, LPCWSTR,
                                          int, DWORD, LPRECT, const DTTOPTS *);
    HANIMATIONBUFFER (WINAPI *begin_animation)(HWND, HDC, const RECT *,
                                                BP_BUFFERFORMAT, BP_PAINTPARAMS *,
                                                BP_ANIMATIONPARAMS *, HDC *, HDC *);
    HDC from = (HDC)1;
    HDC to = (HDC)1;

    module = LoadLibraryA("build\\uxtheme-known\\UXTHEME.DLL");
    if (!module) {
        say("FAIL: LoadLibraryA of built DLL\r\n");
        ExitProcess(1);
    }
    if (!GetModuleFileNameA(module, loaded, sizeof(loaded)) ||
        !contains(loaded, "uxtheme-known")) {
        say("FAIL: loaded different UXTHEME module\r\n");
        ExitProcess(2);
    }
    is_theme_active = (void *)GetProcAddress(module, "IsThemeActive");
    open_theme = (void *)GetProcAddress(module, "OpenThemeData");
    draw_parent_background = (void *)GetProcAddress(module, "DrawThemeParentBackground");
    get_theme_sys_color = (void *)GetProcAddress(module, "GetThemeSysColor");
    draw_theme_text_ex = (void *)GetProcAddress(module, "DrawThemeTextEx");
    begin_animation = (void *)GetProcAddress(module, "BeginBufferedAnimation");
    if (!is_theme_active || !open_theme || !draw_parent_background || !get_theme_sys_color ||
        !draw_theme_text_ex || !begin_animation) {
        say("FAIL: original or added export absent\r\n");
        ExitProcess(3);
    }
    if (is_theme_active() ||
        get_theme_sys_color(NULL, TMT_SCROLLBAR) != GetSysColor(COLOR_SCROLLBAR)) {
        say("FAIL: original KernelEx no-theme/metric behavior\r\n");
        ExitProcess(4);
    }
    SetLastError(0);
    if (open_theme(NULL, L"BUTTON") != NULL ||
        GetLastError() != ERROR_NOT_SUPPORTED ||
        draw_parent_background(NULL, NULL, NULL) != E_HANDLE) {
        say("FAIL: remapped no-theme behavior\r\n");
        ExitProcess(6);
    }
    if (draw_theme_text_ex(NULL, NULL, 0, 0, NULL, 0, 0, NULL, NULL) != E_HANDLE ||
        begin_animation(NULL, NULL, NULL, 0, NULL, NULL, &from, &to) != NULL ||
        from != NULL || to != NULL) {
        say("FAIL: added no-theme behavior\r\n");
        ExitProcess(5);
    }
    FreeLibrary(module);
    say("PASS: merged UXTHEME original and new calls on 32-bit host\r\n");
    ExitProcess(0);
}
