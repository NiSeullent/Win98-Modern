/* Bounded GDI32 alpha/transparent/gradient guest pixel contract.
 * Compile once for a direct KernelEx provider table and once for genuine
 * static GDI32 imports. Pixels are compared to a separate modern Windows
 * 32-bit oracle captured in docs/GDI_ALPHA_GUEST_BASELINE.md.
 * Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only. */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "../src/kex_abi.h"

void *memset(void *destination, int value, size_t count)
{
    volatile BYTE *bytes = (volatile BYTE *)destination;
    size_t i;
    for (i = 0; i < count; ++i) bytes[i] = (BYTE)value;
    return destination;
}

typedef BOOL (WINAPI *alpha_fn)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);
typedef BOOL (WINAPI *transparent_fn)(HDC,int,int,int,int,HDC,int,int,int,int,UINT);
typedef BOOL (WINAPI *gradient_fn)(HDC,PTRIVERTEX,ULONG,PVOID,ULONG,ULONG);
typedef const m98_api_table *(__cdecl *table_fn)(void);
typedef struct surface {
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old;
    DWORD *pixels;
} SURFACE;

static alpha_fn alpha_blend;
static transparent_fn transparent_blt;
static gradient_fn gradient_fill;
static DWORD failures;

static void say(const char *s)
{
    DWORD n = 0, done;
    while (s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &done, NULL);
}

static void hex(DWORD value)
{
    char output[11] = "0x00000000";
    unsigned i;
    for (i = 0; i < 8; ++i)
        output[9 - i] = "0123456789abcdef"[(value >> (i * 4)) & 15];
    say(output);
}

static void fail(const char *name, const char *reason)
{
    ++failures;
    say("FAIL: "); say(name); say(" "); say(reason); say("\r\n");
}

static BOOL create_surface(SURFACE *s, int width, int height)
{
    BITMAPINFO info = {0};
    s->dc = NULL; s->bitmap = NULL; s->old = NULL; s->pixels = NULL;
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    s->dc = CreateCompatibleDC(NULL);
    if (!s->dc) return FALSE;
    s->bitmap = CreateDIBSection(s->dc, &info, DIB_RGB_COLORS,
                                 (void **)&s->pixels, NULL, 0);
    if (!s->bitmap || !s->pixels) return FALSE;
    s->old = SelectObject(s->dc, s->bitmap);
    return s->old && s->old != HGDI_ERROR;
}

static void destroy_surface(SURFACE *s)
{
    if (s->old && s->dc) SelectObject(s->dc, s->old);
    if (s->bitmap) DeleteObject(s->bitmap);
    if (s->dc) DeleteDC(s->dc);
    s->dc = NULL; s->bitmap = NULL; s->pixels = NULL; s->old = NULL;
}

static void verify(const char *name, BOOL result, BOOL expected_result,
                   DWORD error, DWORD expected_error, BOOL check_error,
                   SURFACE *s, const DWORD *expected, unsigned count)
{
    unsigned i;
    GdiFlush();
    say("case="); say(name); say(" result="); hex((DWORD)result);
    say(" gle="); hex(error);
    if (result != expected_result) fail(name, "return value");
    if (check_error && error != expected_error) fail(name, "last error");
    for (i = 0; i < count; ++i) {
        DWORD value = s->pixels[i];
        say(" pixel"); hex(i); say("="); hex(value);
        if (value != expected[i]) fail(name, "pixel/sentinel");
    }
    say("\r\n");
}

static void run_alpha(void)
{
    SURFACE src, dst;
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, 0 };
    BOOL result;
    DWORD error;
    static const DWORD opaque[] = {0x00ff0000, 0x11223344};
    static const DWORD middle[] = {0x0080007f, 0x11223344};
    static const DWORD zero[] = {0x000000ff, 0x11223344};
    static const DWORD per_pixel[] = {0x8080007f, 0x11223344};
    if (!create_surface(&src, 2, 1) || !create_surface(&dst, 2, 1)) {
        fail("alpha", "surface setup"); destroy_surface(&src);
        destroy_surface(&dst); return;
    }
#define RESET_ALPHA() do { src.pixels[0] = 0x00ff0000; \
    src.pixels[1] = 0x11aabbcc; dst.pixels[0] = 0x000000ff; \
    dst.pixels[1] = 0x11223344; } while (0)
    RESET_ALPHA(); SetLastError(0x4938a001);
    result = alpha_blend(dst.dc,0,0,1,1,src.dc,0,0,1,1,blend);
    error = GetLastError(); verify("alpha_opaque",result,TRUE,error,0,FALSE,&dst,opaque,2);
    RESET_ALPHA(); blend.SourceConstantAlpha = 128; SetLastError(0x4938a002);
    result = alpha_blend(dst.dc,0,0,1,1,src.dc,0,0,1,1,blend);
    error = GetLastError(); verify("alpha_constant_128",result,TRUE,error,0,FALSE,&dst,middle,2);
    RESET_ALPHA(); blend.SourceConstantAlpha = 0; SetLastError(0x4938a006);
    result = alpha_blend(dst.dc,0,0,1,1,src.dc,0,0,1,1,blend);
    error = GetLastError(); verify("alpha_constant_0",result,TRUE,error,0,FALSE,&dst,zero,2);
    RESET_ALPHA(); src.pixels[0] = 0x80800000;
    blend.SourceConstantAlpha = 255; blend.AlphaFormat = AC_SRC_ALPHA;
    SetLastError(0x4938a003);
    result = alpha_blend(dst.dc,0,0,1,1,src.dc,0,0,1,1,blend);
    error = GetLastError(); verify("alpha_per_pixel_128",result,TRUE,error,0,FALSE,&dst,per_pixel,2);
    RESET_ALPHA(); SetLastError(0x4938a004);
    result = alpha_blend(NULL,0,0,1,1,src.dc,0,0,1,1,blend);
    error = GetLastError(); verify("alpha_invalid_dest",result,FALSE,error,
                                   ERROR_INVALID_HANDLE,TRUE,&dst,zero,2);
    RESET_ALPHA(); blend.AlphaFormat = 0; SetLastError(0x4938a005);
    result = alpha_blend(dst.dc,0,0,-1,1,src.dc,0,0,1,1,blend);
    error = GetLastError(); verify("alpha_negative_width",result,FALSE,error,
                                   ERROR_INVALID_PARAMETER,TRUE,&dst,zero,2);
#undef RESET_ALPHA
    destroy_surface(&src); destroy_surface(&dst);
}

static void run_transparent(void)
{
    SURFACE src, dst;
    BOOL result;
    DWORD error;
    static const DWORD keyed[] = {0x000000ff,0x00ff0000,0x000000ff,0x000000ff};
    static const DWORD stretched[] = {0x000000ff,0x000000ff,0x00ff0000,0x00ff0000};
    if (!create_surface(&src, 2, 1) || !create_surface(&dst, 4, 1)) {
        fail("transparent", "surface setup"); destroy_surface(&src);
        destroy_surface(&dst); return;
    }
    src.pixels[0] = 0x0000ff00; src.pixels[1] = 0x00ff0000;
#define RESET_TRANSPARENT() do { dst.pixels[0] = 0x000000ff; \
    dst.pixels[1] = 0x000000ff; dst.pixels[2] = 0x000000ff; \
    dst.pixels[3] = 0x000000ff; } while (0)
    RESET_TRANSPARENT(); SetLastError(0x4938b001);
    result = transparent_blt(dst.dc,0,0,2,1,src.dc,0,0,2,1,0x0000ff00);
    error = GetLastError(); verify("transparent_key",result,TRUE,error,0,FALSE,&dst,keyed,4);
    RESET_TRANSPARENT(); SetLastError(0x4938b002);
    result = transparent_blt(dst.dc,0,0,4,1,src.dc,0,0,2,1,0x0000ff00);
    error = GetLastError(); verify("transparent_stretch",result,TRUE,error,0,FALSE,&dst,stretched,4);
    SetLastError(0x4938b003);
    result = transparent_blt(NULL,0,0,1,1,src.dc,0,0,1,1,0);
    error = GetLastError(); verify("transparent_invalid_dest",result,FALSE,error,
                                   ERROR_INVALID_HANDLE,TRUE,&dst,stretched,4);
#undef RESET_TRANSPARENT
    destroy_surface(&src); destroy_surface(&dst);
}

static void run_gradient(void)
{
    SURFACE dst;
    TRIVERTEX vertices[3] = {0};
    GRADIENT_RECT rect = {0,1};
    GRADIENT_TRIANGLE triangle = {0,1,2};
    BOOL result;
    DWORD error;
    static const DWORD horizontal[] = {0x00ff0000,0x00bf003f,0x007f007f,0x003f00bf};
    static const DWORD vertical[] = {0x00ff0000,0x00ff0000,0x00ff0000,0x00ff0000};
    static const DWORD triangular[] = {0x00ff0000,0x00bf3f00,0x007f7f00,0x003fbf00};
    if (!create_surface(&dst,4,4)) {
        fail("gradient", "surface setup"); destroy_surface(&dst); return;
    }
    vertices[0].Red = 0xffff;
    vertices[1].x = 4; vertices[1].y = 4; vertices[1].Blue = 0xffff;
    SetLastError(0x4938c001);
    result = gradient_fill(dst.dc,vertices,2,&rect,1,GRADIENT_FILL_RECT_H);
    error = GetLastError(); verify("gradient_horizontal",result,TRUE,error,0,FALSE,&dst,horizontal,4);
    dst.pixels[0] = dst.pixels[1] = dst.pixels[2] = dst.pixels[3] = 0;
    SetLastError(0x4938c002);
    result = gradient_fill(dst.dc,vertices,2,&rect,1,GRADIENT_FILL_RECT_V);
    error = GetLastError(); verify("gradient_vertical",result,TRUE,error,0,FALSE,&dst,vertical,4);
    vertices[1].x = 0; vertices[1].y = 4;
    vertices[2].x = 4; vertices[2].Green = 0xffff;
    dst.pixels[0] = dst.pixels[1] = dst.pixels[2] = dst.pixels[3] = 0;
    SetLastError(0x4938c003);
    result = gradient_fill(dst.dc,vertices,3,&triangle,1,GRADIENT_FILL_TRIANGLE);
    error = GetLastError(); verify("gradient_triangle",result,TRUE,error,0,FALSE,&dst,triangular,4);
    rect.LowerRight = 9; SetLastError(0x4938c004);
    result = gradient_fill(dst.dc,vertices,2,&rect,1,GRADIENT_FILL_RECT_H);
    error = GetLastError(); verify("gradient_bad_index",result,FALSE,error,
                                   0x4938c004,TRUE,&dst,triangular,4);
    rect.LowerRight = 1; SetLastError(0x4938c005);
    result = gradient_fill(dst.dc,vertices,2,&rect,1,0x77);
    error = GetLastError(); verify("gradient_bad_mode",result,FALSE,error,
                                   ERROR_INVALID_PARAMETER,TRUE,&dst,triangular,4);
    destroy_surface(&dst);
}

#ifndef M98_STATIC_GDI_IMPORTS
static void *lookup(const m98_api_table *table, const char *name)
{
    int i;
    if (!table || !table->target_library ||
        lstrcmpA(table->target_library, "GDI32.DLL") ||
        table->named_apis_count != 3 || table[1].target_library)
        return NULL;
    for (i = 0; i < table->named_apis_count; ++i)
        if (!lstrcmpA(table->named_apis[i].name, name))
            return (void *)(ULONG_PTR)table->named_apis[i].addr;
    return NULL;
}
#endif

void __cdecl mainCRTStartup(void)
{
#ifdef M98_STATIC_GDI_IMPORTS
    alpha_blend = GdiAlphaBlend;
    transparent_blt = GdiTransparentBlt;
    gradient_fill = GdiGradientFill;
    say("mode=static-GDI32\r\n");
#else
    HMODULE provider = LoadLibraryA("C:\\WINDOWS\\KERNELEX\\M98GDI3.DLL");
    table_fn get_table;
    const m98_api_table *table;
    if (!provider) { fail("provider", "LoadLibraryA"); ExitProcess(2); }
    get_table = (table_fn)GetProcAddress(provider, "get_api_table");
    if (!get_table) { fail("provider", "get_api_table"); ExitProcess(2); }
    table = get_table();
    alpha_blend = (alpha_fn)lookup(table, "GdiAlphaBlend");
    transparent_blt = (transparent_fn)lookup(table, "GdiTransparentBlt");
    gradient_fill = (gradient_fn)lookup(table, "GdiGradientFill");
    say("mode=direct-M98GDI3\r\n");
#endif
    if (!alpha_blend || !transparent_blt || !gradient_fill) {
        fail("provider", "three function addresses"); ExitProcess(2);
    }
    run_alpha(); run_transparent(); run_gradient();
    say("internal_failures="); hex(failures); say("\r\n");
    ExitProcess(failures ? 1 : 0);
}
