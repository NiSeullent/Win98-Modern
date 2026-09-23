/*
 * Standard-size RT_GROUP_ICON selection. GPL-2.0-only.
 * Copyright (C) 2026 Win98 Modern contributors.
 *
 * Microsoft's LookupIconIdFromDirectoryEx contract specifies closest size
 * not exceeding the requested size, then display-color-depth tie-break.
 * Original bounded implementation; no Wine/ReactOS body copied.
 */
#include "m98_icon_choice.h"

static unsigned m98_read16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned m98_edge(unsigned char byte)
{
    return byte ? (unsigned)byte : 256U;
}

static unsigned m98_depth(const unsigned char *entry)
{
    unsigned planes = m98_read16(entry + 4);
    unsigned bits = m98_read16(entry + 6);
    if (!planes || !bits || planes > 256 || bits > 256) return 0;
    return planes * bits;
}

/* Exact display depth; otherwise the greatest depth below it; if none,
 * the lowest depth above it. Zero means unspecified and ranks last. */
static int m98_better_depth(unsigned candidate, unsigned best,
                            unsigned display)
{
    if (!candidate) return 0;
    if (!best) return 1;
    if (candidate == display) return best != display;
    if (best == display) return 0;
    if (candidate < display)
        return best > display || candidate > best;
    if (best < display) return 0;
    return candidate < best;
}

int m98_choose_standard_icon(const unsigned char *group, size_t length,
                             int cx, int cy, int display_bpp)
{
    unsigned count, i;
    int best = -1, best_class = 4;
    unsigned best_width = 0, best_height = 0, best_depth = 0;
    unsigned long best_area = 0, best_distance = 0;
    if (!group || length < 6 || display_bpp <= 0 ||
        cx <= 0 || cy <= 0 || cx > 1024 || cy > 1024 ||
        m98_read16(group) != 0 || m98_read16(group + 2) != 1)
        return -1;
    count = m98_read16(group + 4);
    if (!count || count > (length - 6) / 14) return -1;
    for (i = 0; i < count; ++i) {
        const unsigned char *p = group + 6 + i * 14;
        unsigned width = m98_edge(p[0]), height = m98_edge(p[1]);
        unsigned depth = m98_depth(p);
        int kind = width == (unsigned)cx && height == (unsigned)cy ? 0 :
                   (width <= (unsigned)cx && height <= (unsigned)cy ? 1 :
                    (width >= (unsigned)cx && height >= (unsigned)cy ? 2 : 3));
        unsigned long area = (unsigned long)width * height;
        unsigned long distance =
            (unsigned long)(width > (unsigned)cx ? width - cx : cx - width) +
            (unsigned long)(height > (unsigned)cy ? height - cy : cy - height);
        int better_size = best < 0 || kind < best_class ||
            (kind == best_class &&
             ((kind == 1 && area > best_area) ||
              (kind == 2 && area < best_area) ||
              (kind == 3 && distance < best_distance)));
        int same_size = kind == best_class &&
            width == best_width && height == best_height;
        if (better_size ||
            (same_size && m98_better_depth(depth, best_depth,
                                           (unsigned)display_bpp))) {
            best = (int)i;
            best_class = kind;
            best_width = width;
            best_height = height;
            best_area = area;
            best_distance = distance;
            best_depth = depth;
        }
    }
    return best;
}
