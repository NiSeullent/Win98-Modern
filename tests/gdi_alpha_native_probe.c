/* Read-only native MSIMG32 baseline for an installed Windows 98 SE guest.
 * Offscreen DIBs are private to this process. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>

void *memset(void *destination, int value, size_t count)
{
    volatile BYTE *bytes = (volatile BYTE *)destination;
    size_t i;
    for (i = 0; i < count; ++i) bytes[i] = (BYTE)value;
    return destination;
}

#ifndef AC_SRC_OVER
#define AC_SRC_OVER 0
#endif
#ifndef AC_SRC_ALPHA
#define AC_SRC_ALPHA 1
#endif
#ifndef GRADIENT_FILL_RECT_H
#define GRADIENT_FILL_RECT_H 0
#define GRADIENT_FILL_RECT_V 1
#define GRADIENT_FILL_TRIANGLE 2
#endif

typedef struct native_blend {
    BYTE BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat;
} native_blend;
typedef struct native_vertex {
    LONG x, y;
    USHORT Red, Green, Blue, Alpha;
} native_vertex;
typedef struct native_rect { ULONG UpperLeft, LowerRight; } native_rect;
typedef struct native_triangle { ULONG Vertex1, Vertex2, Vertex3; } native_triangle;
typedef BOOL (WINAPI *alpha_fn)(HDC,int,int,int,int,HDC,int,int,int,int,native_blend);
typedef BOOL (WINAPI *trans_fn)(HDC,int,int,int,int,HDC,int,int,int,int,UINT);
typedef BOOL (WINAPI *gradient_fn)(HDC,native_vertex*,ULONG,void*,ULONG,ULONG);

typedef struct surface {
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
    DWORD *pixels;
    int width, height;
} surface;

static alpha_fn alpha_blend;
static trans_fn transparent_blt;
static gradient_fn gradient_fill;
static DWORD failure_count;

static void say(const char *s)
{
    DWORD length = 0, written;
    while (s[length]) ++length;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, length, &written, NULL);
}

static void hex(DWORD value)
{
    char text[11] = "0x00000000";
    unsigned i;
    for (i = 0; i != 8; ++i)
        text[9 - i] = "0123456789abcdef"[(value >> (i * 4)) & 15];
    say(text);
}

static void observe(const char *name, BOOL result, DWORD error, surface *dst)
{
    int i;
    /* DIB bits may be CPU-read only after the GDI batch is flushed. */
    GdiFlush();
    say("case="); say(name);
    say(" result="); hex((DWORD)result);
    say(" gle="); hex(error);
    if (dst && dst->pixels) {
        for (i = 0; i < dst->width && i < 4; ++i) {
            say(" pixel"); hex((DWORD)i); say("=");
            hex(dst->pixels[i]);
        }
    }
    say("\r\n");
}

static BOOL create_surface(surface *s, int width, int height)
{
    BITMAPINFO info;
    DWORD *bits = NULL;
    s->dc = NULL; s->bmp = NULL; s->old = NULL; s->pixels = NULL;
    s->width = width; s->height = height;
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    s->dc = CreateCompatibleDC(NULL);
    if (!s->dc) return FALSE;
    s->bmp = CreateDIBSection(s->dc, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!s->bmp || !bits) return FALSE;
    s->old = SelectObject(s->dc, s->bmp);
    if (!s->old || s->old == HGDI_ERROR) return FALSE;
    s->pixels = bits;
    return TRUE;
}

static void destroy_surface(surface *s)
{
    if (s->old && s->dc) SelectObject(s->dc, s->old);
    if (s->bmp) DeleteObject(s->bmp);
    if (s->dc) DeleteDC(s->dc);
    s->dc = NULL; s->bmp = NULL; s->pixels = NULL;
}

static void run_alpha(void)
{
    surface src, dst;
    native_blend blend;
    BOOL ret;
    DWORD error;
    if (!create_surface(&src, 2, 1) || !create_surface(&dst, 2, 1)) {
        say("FAIL: alpha surface creation\r\n"); ++failure_count;
        destroy_surface(&src); destroy_surface(&dst); return;
    }
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = 0;
    src.pixels[0] = 0x00ff0000;
    dst.pixels[0] = 0x000000ff;
    src.pixels[1] = 0x11aabbcc; dst.pixels[1] = 0x11223344;
    SetLastError(0x4938a001);
    ret = alpha_blend(dst.dc, 0, 0, 1, 1, src.dc, 0, 0, 1, 1, blend);
    error = GetLastError(); observe("alpha_opaque", ret, error, &dst);

    src.pixels[0] = 0x00ff0000;
    dst.pixels[0] = 0x000000ff;
    src.pixels[1] = 0x11aabbcc; dst.pixels[1] = 0x11223344;
    blend.SourceConstantAlpha = 128;
    SetLastError(0x4938a002);
    ret = alpha_blend(dst.dc, 0, 0, 1, 1, src.dc, 0, 0, 1, 1, blend);
    error = GetLastError(); observe("alpha_constant_128", ret, error, &dst);

    src.pixels[0] = 0x00ff0000;
    dst.pixels[0] = 0x000000ff;
    src.pixels[1] = 0x11aabbcc; dst.pixels[1] = 0x11223344;
    blend.SourceConstantAlpha = 0;
    SetLastError(0x4938a006);
    ret = alpha_blend(dst.dc, 0, 0, 1, 1, src.dc, 0, 0, 1, 1, blend);
    error = GetLastError(); observe("alpha_constant_0", ret, error, &dst);

    src.pixels[0] = 0x80800000; /* alpha 128, red 128: premultiplied */
    dst.pixels[0] = 0x000000ff;
    src.pixels[1] = 0x11aabbcc; dst.pixels[1] = 0x11223344;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    SetLastError(0x4938a003);
    ret = alpha_blend(dst.dc, 0, 0, 1, 1, src.dc, 0, 0, 1, 1, blend);
    error = GetLastError(); observe("alpha_per_pixel_128", ret, error, &dst);

    dst.pixels[0] = 0x000000ff;
    dst.pixels[1] = 0x11223344;
    SetLastError(0x4938a004);
    ret = alpha_blend(NULL, 0, 0, 1, 1, src.dc, 0, 0, 1, 1, blend);
    error = GetLastError(); observe("alpha_invalid_dest", ret, error, &dst);

    blend.AlphaFormat = 0;
    dst.pixels[1] = 0x11223344;
    SetLastError(0x4938a005);
    ret = alpha_blend(dst.dc, 0, 0, -1, 1, src.dc, 0, 0, 1, 1, blend);
    error = GetLastError(); observe("alpha_negative_width", ret, error, &dst);
    destroy_surface(&src); destroy_surface(&dst);
}

static void run_transparent(void)
{
    surface src, dst;
    BOOL ret;
    DWORD error;
    if (!create_surface(&src, 2, 1) || !create_surface(&dst, 4, 1)) {
        say("FAIL: transparent surface creation\r\n"); ++failure_count;
        destroy_surface(&src); destroy_surface(&dst); return;
    }
    src.pixels[0] = 0x0000ff00; src.pixels[1] = 0x00ff0000;
    dst.pixels[0] = 0x000000ff; dst.pixels[1] = 0x000000ff;
    dst.pixels[2] = 0x000000ff; dst.pixels[3] = 0x000000ff;
    SetLastError(0x4938b001);
    ret = transparent_blt(dst.dc, 0, 0, 2, 1, src.dc, 0, 0, 2, 1, 0x0000ff00);
    error = GetLastError(); observe("transparent_key", ret, error, &dst);

    dst.pixels[0] = 0x000000ff; dst.pixels[1] = 0x000000ff;
    dst.pixels[2] = 0x000000ff; dst.pixels[3] = 0x000000ff;
    SetLastError(0x4938b002);
    ret = transparent_blt(dst.dc, 0, 0, 4, 1, src.dc, 0, 0, 2, 1, 0x0000ff00);
    error = GetLastError(); observe("transparent_stretch", ret, error, &dst);

    SetLastError(0x4938b003);
    ret = transparent_blt(NULL, 0, 0, 1, 1, src.dc, 0, 0, 1, 1, 0);
    error = GetLastError(); observe("transparent_invalid_dest", ret, error, &dst);
    destroy_surface(&src); destroy_surface(&dst);
}

static void run_gradient(void)
{
    surface dst;
    native_vertex vertices[3];
    native_rect rect;
    native_triangle triangle;
    BOOL ret;
    DWORD error;
    if (!create_surface(&dst, 4, 4)) {
        say("FAIL: gradient surface creation\r\n"); ++failure_count;
        destroy_surface(&dst); return;
    }
    ZeroMemory(vertices, sizeof(vertices));
    vertices[0].x = 0; vertices[0].y = 0; vertices[0].Red = 0xffff;
    vertices[1].x = 4; vertices[1].y = 4; vertices[1].Blue = 0xffff;
    rect.UpperLeft = 0; rect.LowerRight = 1;
    SetLastError(0x4938c001);
    ret = gradient_fill(dst.dc, vertices, 2, &rect, 1, GRADIENT_FILL_RECT_H);
    error = GetLastError(); observe("gradient_horizontal", ret, error, &dst);

    dst.pixels[0] = 0; dst.pixels[1] = 0; dst.pixels[2] = 0; dst.pixels[3] = 0;
    SetLastError(0x4938c002);
    ret = gradient_fill(dst.dc, vertices, 2, &rect, 1, GRADIENT_FILL_RECT_V);
    error = GetLastError(); observe("gradient_vertical", ret, error, &dst);

    vertices[1].x = 0; vertices[1].y = 4; vertices[1].Blue = 0xffff;
    vertices[2].x = 4; vertices[2].y = 0; vertices[2].Green = 0xffff;
    triangle.Vertex1 = 0; triangle.Vertex2 = 1; triangle.Vertex3 = 2;
    dst.pixels[0] = 0; dst.pixels[1] = 0; dst.pixels[2] = 0; dst.pixels[3] = 0;
    SetLastError(0x4938c003);
    ret = gradient_fill(dst.dc, vertices, 3, &triangle, 1, GRADIENT_FILL_TRIANGLE);
    error = GetLastError(); observe("gradient_triangle", ret, error, &dst);

    rect.LowerRight = 9;
    SetLastError(0x4938c004);
    ret = gradient_fill(dst.dc, vertices, 2, &rect, 1, GRADIENT_FILL_RECT_H);
    error = GetLastError(); observe("gradient_bad_index", ret, error, &dst);

    rect.LowerRight = 1;
    SetLastError(0x4938c005);
    ret = gradient_fill(dst.dc, vertices, 2, &rect, 1, 0x77);
    error = GetLastError(); observe("gradient_bad_mode", ret, error, &dst);
    destroy_surface(&dst);
}

void mainCRTStartup(void)
{
    HMODULE msimg, gdi;
    char path[MAX_PATH];
    DWORD path_len;
    const char *command = GetCommandLineA();
    BOOL use_oem = FALSE, use_renamed = FALSE;
    while (*command) {
        if (command[0] == '-' && command[1] == '-' &&
            command[2] == 'o' && command[3] == 'e' && command[4] == 'm' &&
            (!command[5] || command[5] == ' ')) use_oem = TRUE;
        if (command[0] == '-' && command[1] == '-' &&
            command[2] == 'r' && command[3] == 'e' && command[4] == 'n' &&
            command[5] == 'a' && command[6] == 'm' && command[7] == 'e' &&
            command[8] == 'd' && (!command[9] || command[9] == ' '))
            use_renamed = TRUE;
        ++command;
    }
    say("requested=");
    say(use_renamed ? "renamed-oem" : (use_oem ? "system-oem" : "ordinary"));
    say("\r\n");
    msimg = LoadLibraryA(use_renamed ? "C:\\M98LAB\\M98OEMI.DLL" :
                         (use_oem ? "C:\\WINDOWS\\SYSTEM\\MSIMG32.DLL" : "MSIMG32.DLL"));
    gdi = GetModuleHandleA("GDI32.DLL");
    if (!msimg || !gdi) {
        say("FAIL: loading native MSIMG32 or GDI32\r\n"); ExitProcess(2);
    }
    path_len = GetModuleFileNameA(msimg, path, MAX_PATH);
    if (!path_len || path_len == MAX_PATH) {
        say("FAIL: MSIMG32 module path\r\n"); ExitProcess(3);
    }
    path[path_len] = 0;
    say("msimg32_path="); say(path); say("\r\n");
    alpha_blend = (alpha_fn)GetProcAddress(msimg, "AlphaBlend");
    transparent_blt = (trans_fn)GetProcAddress(msimg, "TransparentBlt");
    gradient_fill = (gradient_fn)GetProcAddress(msimg, "GradientFill");
    say("native_resolved="); hex((BOOL)(alpha_blend && transparent_blt && gradient_fill));
    say(" gdi_alpha="); hex((BOOL)GetProcAddress(gdi, "GdiAlphaBlend"));
    say(" gdi_transparent="); hex((BOOL)GetProcAddress(gdi, "GdiTransparentBlt"));
    say(" gdi_gradient="); hex((BOOL)GetProcAddress(gdi, "GdiGradientFill"));
    say("\r\n");
    if (!alpha_blend || !transparent_blt || !gradient_fill) ExitProcess(4);
    run_alpha(); run_transparent(); run_gradient();
    say("internal_failures="); hex(failure_count); say("\r\n");
    ExitProcess(failure_count ? 5 : 0);
}
