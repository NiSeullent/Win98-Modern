/* Three actual GDI32 static imports for a routed Win98 guest. No CRT.
 * The host build checks imports only; guest execution requires M98IMG1.DLL.
 * GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

static void say(const char *message)
{
    DWORD length = 0, done;
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &done, NULL);
}

void __cdecl mainCRTStartup(void)
{
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, 0 };
    DWORD alpha_error, transparent_error, gradient_error;
    BOOL alpha, transparent, gradient;
    SetLastError(0x1234);
    alpha = GdiAlphaBlend(NULL,0,0,1,1,NULL,0,0,1,1,blend);
    alpha_error = GetLastError();
    SetLastError(0x1234);
    transparent = GdiTransparentBlt(NULL,0,0,1,1,NULL,0,0,1,1,0);
    transparent_error = GetLastError();
    SetLastError(0x1234);
    gradient = GdiGradientFill(NULL,NULL,0,NULL,0,GRADIENT_FILL_RECT_H);
    gradient_error = GetLastError();
    if (alpha || transparent || gradient) {
        say("FAIL: invalid HDC unexpectedly accepted\r\n");
        ExitProcess(1);
    }
    if (alpha_error == ERROR_MOD_NOT_FOUND ||
        transparent_error == ERROR_MOD_NOT_FOUND ||
        gradient_error == ERROR_MOD_NOT_FOUND) {
        say("FAIL: copied native graphics backend absent\r\n");
        ExitProcess(2);
    }
    say("PASS: three routed GDI32 static imports returned from backend\r\n");
    ExitProcess(0);
}
