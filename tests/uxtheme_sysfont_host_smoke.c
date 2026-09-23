/* Host smoke for Win98's ANSI-backed GetThemeSysFont replacement.
 * This checks the actual local GUI metrics, all six documented IDs, output
 * guards, and failure paths. It is not a Windows 98 guest validation.
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>

struct guarded_font {
    DWORD before;
    LOGFONTW font;
    DWORD after;
};

static void say(const char *message)
{
    DWORD written;
    DWORD length = 0;
    while (message[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, length, &written, NULL);
}

static BOOL same_font(const LOGFONTW *actual, const LOGFONTA *expected)
{
    WCHAR face[LF_FACESIZE];
    LOGFONTA input = *expected;
    unsigned int index;
    for (index = 0; index < LF_FACESIZE; ++index) face[index] = 0;
    input.lfFaceName[LF_FACESIZE - 1] = 0;
    if (!MultiByteToWideChar(CP_ACP, 0, input.lfFaceName, -1,
                             face, LF_FACESIZE)) return FALSE;
    if (actual->lfHeight != expected->lfHeight ||
        actual->lfWidth != expected->lfWidth ||
        actual->lfEscapement != expected->lfEscapement ||
        actual->lfOrientation != expected->lfOrientation ||
        actual->lfWeight != expected->lfWeight ||
        actual->lfItalic != expected->lfItalic ||
        actual->lfUnderline != expected->lfUnderline ||
        actual->lfStrikeOut != expected->lfStrikeOut ||
        actual->lfCharSet != expected->lfCharSet ||
        actual->lfOutPrecision != expected->lfOutPrecision ||
        actual->lfClipPrecision != expected->lfClipPrecision ||
        actual->lfQuality != expected->lfQuality ||
        actual->lfPitchAndFamily != expected->lfPitchAndFamily) return FALSE;
    for (index = 0; index < LF_FACESIZE; ++index)
        if (actual->lfFaceName[index] != face[index]) return FALSE;
    return TRUE;
}

static BOOL unchanged_font(const LOGFONTW *font)
{
    const BYTE *bytes = (const BYTE *)font;
    unsigned int index;
    for (index = 0; index < sizeof(*font); ++index)
        if (bytes[index] != 0x5a) return FALSE;
    return TRUE;
}

void __cdecl mainCRTStartup(void)
{
#ifndef M98_GUEST_STATIC
    HMODULE module;
#endif
    HRESULT (WINAPI *get_font)(HTHEME, int, LOGFONTW *);
    NONCLIENTMETRICSA metrics;
    LOGFONTA icon;
    const LOGFONTA *expected[6];
    const int ids[6] = {
        TMT_ICONTITLEFONT, TMT_CAPTIONFONT, TMT_SMALLCAPTIONFONT,
        TMT_MENUFONT, TMT_STATUSFONT, TMT_MSGBOXFONT
    };
    struct guarded_font output;
    unsigned int index;
    BYTE *raw;
    unsigned int offset;

#ifdef M98_GUEST_STATIC
    /* Static UXTHEME import exercises KernelEx KnownDLL resolution in Win98. */
    get_font = GetThemeSysFont;
#else
    module = LoadLibraryA("build\\uxtheme-known\\UXTHEME.DLL");
    if (!module) {
        say("FAIL: loading system-font DLL\r\n");
        ExitProcess(1);
    }
    get_font = (void *)GetProcAddress(module, "GetThemeSysFont");
    if (!get_font) {
        say("FAIL: GetThemeSysFont export\r\n");
        ExitProcess(2);
    }
#endif
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoA(SPI_GETICONTITLELOGFONT, sizeof(icon), &icon, 0) ||
        !SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                               &metrics, 0)) {
        say("FAIL: host ANSI system fonts unavailable\r\n");
        ExitProcess(3);
    }
    expected[0] = &icon;
    expected[1] = &metrics.lfCaptionFont;
    expected[2] = &metrics.lfSmCaptionFont;
    expected[3] = &metrics.lfMenuFont;
    expected[4] = &metrics.lfStatusFont;
    expected[5] = &metrics.lfMessageFont;
    for (index = 0; index < 6; ++index) {
        output.before = 0x12345678;
        output.after = 0x87654321;
        if (get_font(NULL, ids[index], &output.font) != S_OK ||
            output.before != 0x12345678 || output.after != 0x87654321 ||
            !same_font(&output.font, expected[index])) {
            say("FAIL: one of six system fonts or output guards\r\n");
            ExitProcess(4);
        }
    }
    raw = (BYTE *)&output.font;
    for (offset = 0; offset < sizeof(output.font); ++offset) raw[offset] = 0x5a;
    if (get_font(NULL, -1, &output.font) != STG_E_INVALIDPARAMETER ||
        !unchanged_font(&output.font) ||
        get_font((HTHEME)1, TMT_ICONTITLEFONT, &output.font) != E_HANDLE ||
        !unchanged_font(&output.font) ||
        get_font(NULL, TMT_ICONTITLEFONT, NULL) != E_POINTER ||
        output.before != 0x12345678 || output.after != 0x87654321) {
        say("FAIL: invalid input changed output or error code\r\n");
        ExitProcess(5);
    }
#ifndef M98_GUEST_STATIC
    FreeLibrary(module);
#endif
    say("PASS: six ANSI-backed UXTHEME system fonts and invalid inputs\r\n");
    ExitProcess(0);
}
