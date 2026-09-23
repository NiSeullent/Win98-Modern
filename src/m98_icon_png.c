/*
 * PNG-compressed RT_ICON decoder for a Win98 icon handle. GPL-2.0-only.
 * Copyright (C) 2026 Win98 Modern contributors.
 *
 * Pinned Wine df15af3 dlls/user32/cursoricon.c and ReactOS 9dc3ca8
 * win32ss/user/user32/windows/cursoricon.c decode PNG and convert it to
 * DIB icon data. Their libpng/NT code is not copied. LodePNG's independent
 * zlib-licensed C decoder is vendored at src/vendor/lodepng; this Win98
 * allocation, bounds, channel, and ownership adapter is original code.
 */
#include "m98_icon_png.h"
#include "vendor/lodepng/lodepng.h"

#define M98_PNG_LIMIT 8388608UL
#define M98_ICON_EDGE_LIMIT 1024UL
#define M98_E_INVALIDARG ((HRESULT)0x80070057UL)
#define M98_E_FAIL ((HRESULT)0x80004005UL)

static DWORD m98_be32(const BYTE *p)
{
    return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) |
           ((DWORD)p[2] << 8) | (DWORD)p[3];
}

static int m98_png_signature(const BYTE *p, DWORD size)
{
    static const BYTE signature[8] = {137,80,78,71,13,10,26,10};
    DWORD i;
    if (!p || size < 33) return 0;
    for (i = 0; i < 8; ++i) if (p[i] != signature[i]) return 0;
    return p[12] == 'I' && p[13] == 'H' && p[14] == 'D' && p[15] == 'R';
}

/* LodePNG was compiled with NO_COMPILE_ALLOCATORS. Win98 process heap is
 * stable across the provider's attach/detach cycle and needs no CRT import. */
void *lodepng_malloc(size_t size)
{
    if (!size || size > M98_PNG_LIMIT) return NULL;
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void *lodepng_realloc(void *ptr, size_t size)
{
    if (size > M98_PNG_LIMIT) return NULL;
    if (!size) {
        if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
        return NULL;
    }
    if (!ptr) return lodepng_malloc(size);
    return HeapReAlloc(GetProcessHeap(), 0, ptr, size);
}

void lodepng_free(void *ptr)
{
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

HRESULT m98_icon_from_png(const BYTE *png, DWORD length, int cx, int cy,
                          HICON *icon)
{
    DWORD declared_width, declared_height;
    unsigned width = 0, height = 0, error;
    unsigned char *rgba = NULL;
    BYTE *dib = NULL, *pixels, *mask;
    DWORD stride, image_size, mask_size, total;
    DWORD x, y;
    BITMAPINFOHEADER *header;
    HICON result;
    if (!icon || !m98_png_signature(png, length) ||
        length > M98_PNG_LIMIT || cx <= 0 || cy <= 0 ||
        cx > (int)M98_ICON_EDGE_LIMIT || cy > (int)M98_ICON_EDGE_LIMIT)
        return M98_E_INVALIDARG;
    *icon = NULL;
    declared_width = m98_be32(png + 16);
    declared_height = m98_be32(png + 20);
    if (!declared_width || !declared_height ||
        declared_width > M98_ICON_EDGE_LIMIT ||
        declared_height > M98_ICON_EDGE_LIMIT ||
        declared_width * declared_height > 1048576UL)
        return M98_E_INVALIDARG;
    error = lodepng_decode32(&rgba, &width, &height, png, length);
    if (error || !rgba || width != declared_width ||
        height != declared_height) {
        lodepng_free(rgba);
        return error == 83 ? E_OUTOFMEMORY : M98_E_INVALIDARG;
    }
    stride = ((width + 31) / 32) * 4;
    image_size = width * height * 4;
    mask_size = stride * height;
    total = sizeof(BITMAPINFOHEADER) + image_size + mask_size;
    if (total > M98_PNG_LIMIT) {
        lodepng_free(rgba);
        return E_OUTOFMEMORY;
    }
    dib = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total);
    if (!dib) {
        lodepng_free(rgba);
        return E_OUTOFMEMORY;
    }
    header = (BITMAPINFOHEADER *)dib;
    header->biSize = sizeof(*header);
    header->biWidth = (LONG)width;
    header->biHeight = (LONG)(height * 2);
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biCompression = BI_RGB;
    header->biSizeImage = image_size;
    pixels = dib + sizeof(*header);
    mask = pixels + image_size;
    for (y = 0; y < height; ++y) {
        const BYTE *source = rgba + (height - y - 1) * width * 4;
        BYTE *destination = pixels + y * width * 4;
        BYTE *mask_row = mask + y * stride;
        for (x = 0; x < width; ++x) {
            destination[x * 4 + 0] = source[x * 4 + 2];
            destination[x * 4 + 1] = source[x * 4 + 1];
            destination[x * 4 + 2] = source[x * 4 + 0];
            destination[x * 4 + 3] = source[x * 4 + 3];
            if (source[x * 4 + 3] < 128)
                mask_row[x >> 3] |= (BYTE)(0x80 >> (x & 7));
        }
    }
    result = CreateIconFromResourceEx(dib, total, TRUE, 0x00030000,
                                      cx, cy, LR_DEFAULTCOLOR);
    HeapFree(GetProcessHeap(), 0, dib);
    lodepng_free(rgba);
    if (!result) {
        DWORD last = GetLastError();
        return last ? HRESULT_FROM_WIN32(last) : M98_E_FAIL;
    }
    *icon = result;
    return S_OK;
}
