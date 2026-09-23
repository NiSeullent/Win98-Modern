/* Copyright (C) 2026 Win98-Modern contributors. GPL-2.0-only.
 * Original Win98 adapter. Pinned Wine df15af3652511150490934682202d45af892f887
 * dlls/gdi32/dc.c and dlls/win32u/{bitblt,painting}.c, and pinned ReactOS
 * 9dc3ca87209fd8ebabd96c8ea95d439c13e7fdf8 gdi32/objects/painting.c,
 * ntgdi/bitblt.c, eng/alphablend.c were reviewed. Their NT objects and bodies
 * are not copied. Both modern MSIMG32 libraries forward to GDI32; this Win98
 * path requires the independently implemented installed KernelEx auxiliary
 * MSIMG32. The original OEM MSIMG32 was rejected after guest overdraw tests.
 * See docs/GDI_ALPHA_FAMILY_AUDIT.md and docs/GDI_ALPHA_PORT.md.
 */
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0410
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include "m98_gdi_alpha.h"

typedef BOOL (WINAPI *alpha_fn)(HDC,int,int,int,int,HDC,int,int,int,int,BLENDFUNCTION);
typedef BOOL (WINAPI *transparent_fn)(HDC,int,int,int,int,HDC,int,int,int,int,UINT);
typedef BOOL (WINAPI *gradient_fn)(HDC,PTRIVERTEX,ULONG,PVOID,ULONG,ULONG);

static volatile LONG tls_slot = -1;
static HINSTANCE self_module;

#define M98_MAX_RASTER_SIDE 8192
#define M98_MAX_RASTER_PIXELS 4194304
#define M98_MAX_GRADIENT_VERTICES 4096
#define M98_MAX_GRADIENT_MESH 1024
#define M98_MAX_GRADIENT_WORK 1048576

void m98_gdi_alpha_attach(HINSTANCE instance)
{
    self_module = instance;
}

void m98_gdi_alpha_detach(void)
{
    LONG old = InterlockedExchange(&tls_slot, -1);
    /* TlsAlloc is lazy so the slot may never have been allocated. KernelEx
     * normally retains the provider; explicit fixture unload must still
     * return its process-global TLS slot. No MSIMG32 loading occurs here. */
    if (old != -1) TlsFree((DWORD)old);
    self_module = NULL;
}

static BOOL m98_equal_ascii(const char *a, const char *b, BOOL ignore_case)
{
    for (;;) {
        unsigned char x = (unsigned char)*a++;
        unsigned char y = (unsigned char)*b++;
        if (ignore_case) {
            if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
            if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        }
        if (x != y) return FALSE;
        if (!x) return TRUE;
    }
}

static BOOL m98_range(DWORD rva, DWORD bytes, DWORD image_size)
{
    return rva <= image_size && bytes <= image_size - rva;
}

/* GetProcAddress follows PE forwarders. Reject them before calling through a
 * GDI32 route, or modern MSIMG32 -> GDI32 forwarding can recurse indefinitely.
 * Only the exact named independent code RVA in the loaded system image passes. */
static BOOL m98_export_is_code(HMODULE module, const char *name,
                               DWORD expected_rva, FARPROC proc)
{
    const BYTE *base = (const BYTE *)module;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS32 *nt;
    const IMAGE_DATA_DIRECTORY *dir;
    const IMAGE_EXPORT_DIRECTORY *exp;
    const DWORD *names, *functions;
    const WORD *ordinals;
    DWORD image_size, i, entry;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        dos->e_lfanew > 0x100000) return FALSE;
    nt = (const IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
        return FALSE;
    /* This exact layout was read from the installed KernelEx auxiliary DLL:
     * SHA-256 e2a644c38cd814a63239b0376cb9921d824c492b8816781ae7f3e8724efabee5.
     * The timestamp/image/RVAs are a runtime version gate; the SHA receipt is
     * verified by the installer/tester, not assumed from these fields alone. */
    if (nt->FileHeader.TimeDateStamp != 0x4ec18837 ||
        nt->OptionalHeader.SizeOfImage != 0x5000) return FALSE;
    image_size = nt->OptionalHeader.SizeOfImage;
    if (image_size < 4096 || image_size > 0x10000000) return FALSE;
    dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir->VirtualAddress ||
        !m98_range(dir->VirtualAddress, dir->Size, image_size) ||
        dir->Size < sizeof(*exp)) return FALSE;
    exp = (const IMAGE_EXPORT_DIRECTORY *)(base + dir->VirtualAddress);
    if (!exp->NumberOfNames || exp->NumberOfNames > 65536 ||
        !exp->NumberOfFunctions || exp->NumberOfFunctions > 65536 ||
        !m98_range(exp->AddressOfNames, exp->NumberOfNames * sizeof(DWORD), image_size) ||
        !m98_range(exp->AddressOfNameOrdinals, exp->NumberOfNames * sizeof(WORD), image_size) ||
        !m98_range(exp->AddressOfFunctions, exp->NumberOfFunctions * sizeof(DWORD), image_size))
        return FALSE;
    names = (const DWORD *)(base + exp->AddressOfNames);
    ordinals = (const WORD *)(base + exp->AddressOfNameOrdinals);
    functions = (const DWORD *)(base + exp->AddressOfFunctions);
    for (i = 0; i < exp->NumberOfNames; ++i) {
        DWORD rva = names[i], p = 0;
        if (!m98_range(rva, 1, image_size)) return FALSE;
        while (name[p] && m98_range(rva + p, 1, image_size) &&
               base[rva + p] == (BYTE)name[p]) ++p;
        if (name[p] || !m98_range(rva + p, 1, image_size) || base[rva + p])
            continue;
        if (ordinals[i] >= exp->NumberOfFunctions) return FALSE;
        entry = functions[ordinals[i]];
        if (entry != expected_rva || !m98_range(entry, 1, image_size) ||
            (entry >= dir->VirtualAddress && entry - dir->VirtualAddress < dir->Size))
            return FALSE;
        return (FARPROC)(base + entry) == proc;
    }
    return FALSE;
}

static DWORD m98_tls_index(void)
{
    LONG current = InterlockedCompareExchange(&tls_slot, -1, -1);
    DWORD fresh;
    if (current != -1) return (DWORD)current;
    fresh = TlsAlloc();
    if (fresh == TLS_OUT_OF_INDEXES) return TLS_OUT_OF_INDEXES;
    current = InterlockedCompareExchange(&tls_slot, (LONG)fresh, -1);
    if (current != -1) {
        TlsFree(fresh);
        return (DWORD)current;
    }
    return fresh;
}

/* Every call holds its own reference; no pointer survives FreeLibrary. The
 * TLS guard also fails closed if a non-forwarder trampoline reenters us. */
static FARPROC m98_begin(const char *name, DWORD expected_rva,
                         HMODULE *module, DWORD *tls_index)
{
    /* Use only the pinned installed KernelEx auxiliary module beside this
     * provider. The OEM binary has observed out-of-rectangle writes, and a
     * modern forwarding MSIMG32 could recurse through these GDI32 routes. */
    static const char library[] = "MSIMG32.DLL";
    char path[MAX_PATH], actual[MAX_PATH];
    UINT length, i;
    DWORD error;
    FARPROC proc;

    *module = NULL;
    *tls_index = m98_tls_index();
    if (*tls_index == TLS_OUT_OF_INDEXES) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (TlsGetValue(*tls_index)) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }
    if (!TlsSetValue(*tls_index, (LPVOID)1)) return NULL;
    if (!self_module) {
        SetLastError(ERROR_DLL_INIT_FAILED);
        goto fail;
    }
    length = GetModuleFileNameA(self_module, path, sizeof(path));
    if (!length) goto fail;
    if (length >= sizeof(path)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto fail;
    }
    while (length && path[length - 1] != '\\' && path[length - 1] != '/') --length;
    if (!length || length + sizeof(library) > sizeof(path)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto fail;
    }
    for (i = 0; i < sizeof(library); ++i) path[length + i] = library[i];
    *module = LoadLibraryA(path);
    if (!*module) goto fail;
    length = GetModuleFileNameA(*module, actual, sizeof(actual));
    if (!length || length >= sizeof(actual) ||
        !m98_equal_ascii(path, actual, TRUE)) {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        goto fail;
    }
    proc = GetProcAddress(*module, name);
    if (!proc) goto fail;
    if (!m98_export_is_code(*module, name, expected_rva, proc)) {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        goto fail;
    }
    return proc;
fail:
    error = GetLastError();
    if (*module) FreeLibrary(*module);
    *module = NULL;
    TlsSetValue(*tls_index, NULL);
    SetLastError(error);
    return NULL;
}

static void m98_end(HMODULE module, DWORD tls_index, DWORD native_error)
{
    TlsSetValue(tls_index, NULL);
    FreeLibrary(module);
    SetLastError(native_error);
}

static BOOL m98_raster_args(HDC dest, int dx, int dy, int dw, int dh,
                            HDC source, int sx, int sy, int sw, int sh)
{
    if (!dest || !source) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    /* The installed auxiliary backend uses signed width*height arithmetic,
     * unchecked temporary bitmap creation and 32-bit GDI coordinate maths.
     * This explicit bounded domain avoids overflow and extreme allocation. */
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 ||
        dw > M98_MAX_RASTER_SIDE || dh > M98_MAX_RASTER_SIDE ||
        sw > M98_MAX_RASTER_SIDE || sh > M98_MAX_RASTER_SIDE ||
        (DWORD)dw * (DWORD)dh > M98_MAX_RASTER_PIXELS ||
        (DWORD)sw * (DWORD)sh > M98_MAX_RASTER_PIXELS ||
        dx < -1048576 || dx > 1048576 || dy < -1048576 || dy > 1048576 ||
        sx < -1048576 || sx > 1048576 || sy < -1048576 || sy > 1048576) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

static LONG m98_abs_diff(LONG a, LONG b)
{
    return a < b ? b - a : a - b;
}

static BOOL m98_gradient_args(HDC dc, PTRIVERTEX vertices, ULONG vertex_count,
                              PVOID mesh, ULONG mesh_count, ULONG mode,
                              DWORD incoming_error)
{
    ULONG i;
    DWORD work = 0;
    if (!dc) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!vertices || !mesh || !vertex_count || !mesh_count ||
        vertex_count > M98_MAX_GRADIENT_VERTICES ||
        mesh_count > M98_MAX_GRADIENT_MESH ||
        (mode != GRADIENT_FILL_RECT_H && mode != GRADIENT_FILL_RECT_V &&
         mode != GRADIENT_FILL_TRIANGLE)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (IsBadReadPtr(vertices, vertex_count * sizeof(TRIVERTEX)) ||
        IsBadReadPtr(mesh, mesh_count * (mode == GRADIENT_FILL_TRIANGLE ?
                                      sizeof(GRADIENT_TRIANGLE) : sizeof(GRADIENT_RECT)))) {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    for (i = 0; i < vertex_count; ++i) {
        if (vertices[i].x < -32767 || vertices[i].x > 32767 ||
            vertices[i].y < -32767 || vertices[i].y > 32767) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }
    for (i = 0; i < mesh_count; ++i) {
        ULONG a, b, c;
        LONG width, height;
        if (mode == GRADIENT_FILL_TRIANGLE) {
            const GRADIENT_TRIANGLE *triangles = (const GRADIENT_TRIANGLE *)mesh;
            a = triangles[i].Vertex1; b = triangles[i].Vertex2;
            c = triangles[i].Vertex3;
            if (a >= vertex_count || b >= vertex_count || c >= vertex_count) {
                /* Windows 11 returns FALSE without changing the caller's GLE
                 * for this observed invalid-index case. KernelEx returned
                 * TRUE after reading past the vertex array. */
                SetLastError(incoming_error);
                return FALSE;
            }
            width = m98_abs_diff(vertices[a].x, vertices[b].x);
            if (m98_abs_diff(vertices[a].x, vertices[c].x) > width)
                width = m98_abs_diff(vertices[a].x, vertices[c].x);
            if (m98_abs_diff(vertices[b].x, vertices[c].x) > width)
                width = m98_abs_diff(vertices[b].x, vertices[c].x);
            height = m98_abs_diff(vertices[a].y, vertices[b].y);
            if (m98_abs_diff(vertices[a].y, vertices[c].y) > height)
                height = m98_abs_diff(vertices[a].y, vertices[c].y);
            if (m98_abs_diff(vertices[b].y, vertices[c].y) > height)
                height = m98_abs_diff(vertices[b].y, vertices[c].y);
            if (width > M98_MAX_RASTER_SIDE || height > M98_MAX_RASTER_SIDE ||
                (DWORD)width * (DWORD)height > M98_MAX_GRADIENT_WORK - work) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            work += (DWORD)width * (DWORD)height;
        } else {
            const GRADIENT_RECT *rectangles = (const GRADIENT_RECT *)mesh;
            a = rectangles[i].UpperLeft; b = rectangles[i].LowerRight;
            if (a >= vertex_count || b >= vertex_count) {
                SetLastError(incoming_error);
                return FALSE;
            }
            width = m98_abs_diff(vertices[a].x, vertices[b].x);
            height = m98_abs_diff(vertices[a].y, vertices[b].y);
            /* KernelEx creates one strip per interpolated axis but draws the
             * full perpendicular span for every strip. Count the rasterized
             * area, not just the number of strip/Pen iterations. */
            if (width > M98_MAX_RASTER_SIDE || height > M98_MAX_RASTER_SIDE ||
                (DWORD)width * (DWORD)height > M98_MAX_GRADIENT_WORK - work) {
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            work += (DWORD)width * (DWORD)height;
        }
    }
    return TRUE;
}

BOOL WINAPI m98_GdiAlphaBlend(HDC dest, int dx, int dy, int dw, int dh,
                               HDC source, int sx, int sy, int sw, int sh,
                               BLENDFUNCTION blend)
{
    DWORD before = GetLastError(), slot, native_error;
    HMODULE module;
    alpha_fn native;
    BOOL result;
    if (!m98_raster_args(dest, dx, dy, dw, dh, source, sx, sy, sw, sh))
        return FALSE;
    if (blend.BlendOp != AC_SRC_OVER || blend.BlendFlags ||
        (blend.AlphaFormat != 0 && blend.AlphaFormat != AC_SRC_ALPHA)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    native = (alpha_fn)m98_begin("AlphaBlend", 0x1020, &module, &slot);
    if (!native) return FALSE;
    SetLastError(before);
    result = native(dest, dx, dy, dw, dh, source, sx, sy, sw, sh, blend);
    native_error = GetLastError();
    m98_end(module, slot, native_error);
    return result;
}

BOOL WINAPI m98_GdiTransparentBlt(HDC dest, int dx, int dy, int dw, int dh,
                                   HDC source, int sx, int sy, int sw, int sh,
                                   UINT transparent)
{
    DWORD before = GetLastError(), slot, native_error;
    HMODULE module;
    transparent_fn native;
    BOOL result;
    if (!m98_raster_args(dest, dx, dy, dw, dh, source, sx, sy, sw, sh))
        return FALSE;
    native = (transparent_fn)m98_begin("TransparentBlt", 0x1c10, &module, &slot);
    if (!native) return FALSE;
    SetLastError(before);
    result = native(dest, dx, dy, dw, dh, source, sx, sy, sw, sh, transparent);
    native_error = GetLastError();
    m98_end(module, slot, native_error);
    return result;
}

BOOL WINAPI m98_GdiGradientFill(HDC dc, PTRIVERTEX vertices, ULONG vertex_count,
                                 PVOID mesh, ULONG mesh_count, ULONG mode)
{
    DWORD before = GetLastError(), slot, native_error;
    HMODULE module;
    gradient_fn native;
    BOOL result;
    if (!m98_gradient_args(dc, vertices, vertex_count, mesh, mesh_count,
                           mode, before)) return FALSE;
    native = (gradient_fn)m98_begin("GradientFill", 0x1470, &module, &slot);
    if (!native) return FALSE;
    SetLastError(before);
    result = native(dc, vertices, vertex_count, mesh, mesh_count, mode);
    native_error = GetLastError();
    m98_end(module, slot, native_error);
    return result;
}
