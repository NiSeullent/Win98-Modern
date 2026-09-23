/* Pure standard-size and display-depth selector contract. GPL-2.0-only. */
#include "../src/m98_icon_choice.h"
#include <stdio.h>

static unsigned char group[6 + 14 * 4];

static void set_entry(int index, int edge, int bpp)
{
    unsigned char *p = group + 6 + index * 14;
    p[0] = (unsigned char)edge;
    p[1] = (unsigned char)edge;
    p[4] = 1;
    p[6] = (unsigned char)bpp;
    p[12] = (unsigned char)(index + 1);
}

static int expect(int actual, int wanted, const char *label)
{
    if (actual == wanted) return 0;
    printf("FAIL: %s, got %d expected %d\n", label, actual, wanted);
    return 1;
}

int main(void)
{
    int failures = 0;
    group[2] = 1;
    group[4] = 2;
    set_entry(0, 16, 32);
    set_entry(1, 48, 32);
    failures += expect(m98_choose_standard_icon(group, 34, 32, 32, 16),
                       0, "standard 32 picks 16 source");
    failures += expect(m98_choose_standard_icon(group, 34, 48, 48, 16),
                       1, "exact 48 source");

    /* Identical 32px dimensions: display depth chooses the image. */
    set_entry(0, 32, 16);
    set_entry(1, 32, 32);
    failures += expect(m98_choose_standard_icon(group, 34, 32, 32, 16),
                       0, "16bpp screen chooses 16bpp image");
    failures += expect(m98_choose_standard_icon(group, 34, 32, 32, 32),
                       1, "32bpp screen chooses 32bpp image");
    failures += expect(m98_choose_standard_icon(group, 34, 32, 32, 24),
                       0, "24bpp screen chooses greatest below");
    failures += expect(m98_choose_standard_icon(group, 34, 32, 32, 8),
                       0, "8bpp screen chooses lowest above");
    failures += expect(m98_choose_standard_icon(group, 34, 32, 32, 4),
                       0, "4bpp screen chooses lowest above");

    /* Three depths prove the greatest-not-exceeding rule, independent of
     * resource ordering. */
    group[4] = 3;
    set_entry(0, 32, 32);
    set_entry(1, 32, 8);
    set_entry(2, 32, 16);
    failures += expect(m98_choose_standard_icon(group, 48, 32, 32, 16),
                       2, "exact 16bpp wins among three");
    failures += expect(m98_choose_standard_icon(group, 48, 32, 32, 32),
                       0, "exact 32bpp wins among three");
    failures += expect(m98_choose_standard_icon(group, 48, 32, 32, 24),
                       2, "best depth below 24");

    failures += expect(m98_choose_standard_icon(group, 47, 32, 32, 16),
                       -1, "truncated resource rejected");
    failures += expect(m98_choose_standard_icon(group, 48, 32, 32, 0),
                       -1, "invalid display depth rejected");
    if (!failures) puts("PASS: standard icon size and 4/8/16/24/32bpp depth selection");
    return failures ? 1 : 0;
}
