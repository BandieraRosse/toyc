#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rf_boot_canvas.h"

/* Stable presentation contracts: padding, temporal composition, coalescing,
 * cancellation. No page coordinates or text snapshots. */
static uint32_t pixels[72][132];
static struct toy_surface surface = {&pixels[0][0], 128, 72, sizeof(pixels[0])};

static void paint(uint32_t value)
{
    for (int y = 0; y < surface.height; ++y) {
        for (int x = 0; x < surface.width; ++x) pixels[y][x] = value;
        for (int x = surface.width; x < 132; ++x) pixels[y][x] = 0xdeadbeef;
    }
}

static void frame(struct rf_boot_canvas *c, int page, int64_t now, int immediate,
                   uint32_t color, int region)
{
    paint(color);
    rf_boot_canvas_begin(c);
    if (region) rf_boot_canvas_region(c, 0, 0, 640, 720);
    rf_boot_canvas_compose(c, &surface, page, now, immediate);
    for (int y = 0; y < surface.height; ++y)
        for (int x = surface.width; x < 132; ++x) assert(pixels[y][x] == 0xdeadbeef);
}

int main(void)
{
    struct rf_boot_canvas c = {0};
    frame(&c, 0, 0, 0, 0x111111, 1);
    assert(pixels[71][0] == 0x111111);
    frame(&c, 1, 1000000, 0, 0x222222, 1);
    assert(pixels[0][0] == 0x111111 && pixels[71][0] == 0x111111);
    frame(&c, 1, 1120000, 0, 0x222222, 1);
    assert(pixels[0][0] == 0x222222 && pixels[71][0] == 0x111111);
    frame(&c, 1, 1240000, 0, 0x222222, 1);
    assert(pixels[71][0] == 0x222222 && !c.sweeping);

    frame(&c, 1, 1300000, 0, 0x333333, 1);
    assert(pixels[71][0] == 0x222222 && pixels[71][127] == 0x333333);
    frame(&c, 1, 1340000, 0, 0x444444, 1);
    assert(pixels[0][0] == 0x444444 && pixels[71][0] == 0x222222);
    frame(&c, 1, 1380000, 0, 0x555555, 1);
    assert(pixels[71][0] == 0x555555); /* Latest content, original deadline. */
    frame(&c, 1, 1400000, 0, 0x555555, 1);
    assert(!c.regions[0].active);

    frame(&c, 1, 1410000, 0, 0x555555, 0);
    frame(&c, 1, 1420000, 0, 0x565656, 1);
    assert(pixels[71][0] == 0x555555); /* A newly appended region scans too. */

    frame(&c, 2, 1500000, 0, 0x666666, 0);
    frame(&c, 2, 1510000, 1, 0x666666, 0);
    assert(pixels[71][0] == 0x666666 && !c.sweeping);
    {
        struct boot_rect r = {51, 99, 253, 45};
        int vw, vh, ox, oy;
        boot_view(&surface, &vw, &vh, &ox, &oy);
        int left = ox + r.x * vw / 1280;
        int top = oy + r.y * vh / 720;
        int right = ox + (r.x + r.w) * vw / 1280;
        assert(boot_hit_rect(&surface, &r, left, top));
        assert(!boot_hit_rect(&surface, &r, left - 1, top));
        assert(!boot_hit_rect(&surface, &r, right, top));
    }
    rf_boot_canvas_destroy(&c);
    assert(!c.shown && !c.source && !c.target);
    puts("boot canvas: padded stride, row scan, coalescing, input completion and hit regions PASS");
    return 0;
}
