/* Bounded RT_GROUP_ICON selection for standard icon sizes. GPL-2.0-only. */
#ifndef M98_ICON_CHOICE_H
#define M98_ICON_CHOICE_H

#include <stddef.h>

/* Returns a zero-based GRPICONDIRENTRY index, or -1 for malformed input. */
int m98_choose_standard_icon(const unsigned char *group, size_t length,
                             int cx, int cy, int display_bpp);

#endif
