#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern void pc_draw_env(int, int, int, int, int, int, int);
extern void pc_draw_packet(void*, int);
extern void pc_vram_clear(int, int, int, int, int, int, int);
extern void pc_vram_transfer(int, int, int, int, void*, int);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
#define XY(x, y) ((uint16_t)(x) | ((uint32_t)(uint16_t)(y) << 16))
static uint16_t read_pixel(int x, int y)
{
    uint16_t p;
    pc_vram_transfer(x, y, 1, 1, &p, 0);
    return p;
}
static void commands(uint32_t* p, int n)
{
    p[0] = (unsigned)n << 24 | 0xffffff;
    pc_draw_packet(p, 0);
}
int main(void)
{
    pc_draw_env(0, 0, 32, 32, 0, 0, 0);
    pc_vram_clear(0, 0, 32, 32, 0, 0, 0);
    uint32_t flat[] = { 0, 0x200000ff, XY(2, 2), XY(10, 2), XY(2, 10) };
    commands(flat, 4);
    CHECK(read_pixel(3, 3) == 31 && read_pixel(11, 3) == 0);
    /* Reversing winding preserves coverage. */
    flat[2] = XY(2, 10);
    flat[4] = XY(2, 2);
    pc_vram_clear(0, 0, 32, 32, 0, 0, 0);
    commands(flat, 4);
    CHECK(read_pixel(3, 3) == 31 && read_pixel(11, 3) == 0);
    uint32_t quad[] = { 0, 0x2a000080, XY(2, 2), XY(10, 2), XY(2, 10), XY(10, 10) };
    pc_draw_env(
        0, 0, 32, 32, 0, 0, 32); /* additive blend: double-hit edges become obvious */
    pc_vram_clear(0, 0, 32, 32, 0, 0, 0);
    commands(quad, 5);
    for (int y = 2; y < 10; ++y)
        for (int x = 2; x < 10; ++x)
            CHECK(read_pixel(x, y) == 16);
    uint32_t gouraud[] = { 0, 0x300000ff, XY(2, 2), 0x0000ff00, XY(14, 2), 0x00ff0000,
        XY(2, 14) };
    pc_vram_clear(0, 0, 32, 32, 0, 0, 0);
    commands(gouraud, 6);
    uint16_t center = read_pixel(5, 5);
    CHECK((center & 31) && (center & 0x3e0) && (center & 0x7c00));
    uint16_t texel = 0x001f;
    pc_vram_transfer(640, 0, 1, 1, &texel, 1);
    uint32_t textured[] = { 0, 0x25808080, XY(2, 2), 0, XY(10, 2), 0x014a0000, XY(2, 10),
        0 };
    pc_vram_clear(0, 0, 32, 32, 0, 0, 0);
    commands(textured, 7);
    CHECK(read_pixel(3, 3) == 31); /* 15-bit raw texture; UV and texpage decoding */
    texel = 0;
    pc_vram_transfer(640, 0, 1, 1, &texel, 1);
    pc_vram_clear(0, 0, 32, 32, 0, 0, 255);
    commands(textured, 7);
    CHECK(read_pixel(3, 3) == 0x7c00); /* zero texel is transparent */
    uint16_t index = 1, green = 0x3e0;
    pc_vram_transfer(640, 0, 1, 1, &index, 1);
    pc_vram_transfer(1, 64, 1, 1, &green, 1);
    textured[3] = 0x10000000;
    textured[5] = 0x000a0000;
    commands(textured, 7);
    CHECK(read_pixel(3, 3) == green); /* 4-bit CLUT */
    textured[5] = 0x008a0000;
    commands(textured, 7);
    CHECK(read_pixel(3, 3) == green); /* 8-bit CLUT */
    uint16_t blue = 0x7c00;
    pc_vram_transfer(648, 0, 1, 1, &blue, 1);
    uint32_t window[] = { 0, 0xe2000401 };
    commands(window, 1);
    textured[5] = 0x014a0000;
    commands(textured, 7);
    CHECK(read_pixel(3, 3) == blue);
    window[1] = 0xe2000000;
    commands(window, 1);
    uint16_t edge_color = 0x1234, wrap_color = 0x5678;
    pc_vram_transfer(1023, 511, 1, 1, &edge_color, 1);
    pc_vram_transfer(0, 511, 1, 1, &wrap_color, 1);
    uint32_t copy[] = { 0, 0x80000000, XY(1023, 511), XY(40, 40), XY(2, 1) };
    commands(copy, 4);
    CHECK(read_pixel(40, 40) == edge_color && read_pixel(41, 40) == wrap_color);
    uint32_t mask[] = { 0, 0xe6000001 };
    commands(mask, 1);
    commands(flat, 4);
    CHECK(read_pixel(3, 3) == 0x801f);
    mask[1] = 0xe6000002;
    commands(mask, 1);
    flat[1] = 0x2000ff00;
    commands(flat, 4);
    CHECK(read_pixel(3, 3) == 0x801f);
    mask[1] = 0xe6000000;
    commands(mask, 1);
    pc_draw_env(0, 0, 32, 32, 0, 0, 32);
    uint32_t area[] = { 0, 0xe3000000 | 4 | (4 << 10), 0xe4000000 | 7 | (7 << 10) };
    pc_vram_clear(0, 0, 32, 32, 0, 0, 0);
    commands(area, 2);
    commands(quad, 5);
    CHECK(read_pixel(3, 3) == 0 && read_pixel(4, 4) == 16 && read_pixel(8, 4) == 0);
    pc_draw_env(0, 0, 32, 32, 0, 0, 0);
    pc_vram_clear(0, 0, 32, 32, 0, 0, 0);
    uint32_t line[] = { 0, 0x400000ff, XY(2, 2), XY(8, 2) };
    commands(line, 3);
    CHECK(read_pixel(2, 2) == 31 && read_pixel(8, 2) == 31 && read_pixel(9, 2) == 0);
    uint32_t polyline[] = { 0, 0x4800ff00, XY(10, 2), XY(14, 2), XY(14, 6), 0x55555555,
        0x400000ff, XY(20, 2), XY(20, 4) };
    commands(polyline, 8);
    CHECK(read_pixel(12, 2) == 0x3e0 && read_pixel(14, 4) == 0x3e0
          && read_pixel(20, 3) == 31);
    uint32_t gradient[] = { 0, 0x580000ff, XY(2, 10), 0xff0000, XY(10, 10), 0x00ff00,
        XY(10, 18), 0x50005000 };
    commands(gradient, 7);
    CHECK(read_pixel(2, 10) == 31 && read_pixel(10, 10) == 0x7c00
          && read_pixel(10, 18) == 0x3e0);
    CHECK(read_pixel(6, 10) == (15 | (15 << 10)));
    pc_draw_env(4, 4, 4, 4, 5, 5, 32);
    uint32_t clipped_line[] = { 0, 0x42000040, XY(-4, 0), XY(4, 0) };
    commands(clipped_line, 3);
    CHECK(read_pixel(3, 5) == 0 && read_pixel(4, 5) == 8 && read_pixel(7, 5) == 8
          && read_pixel(8, 5) == 0);
    /* Fixed-size flat rectangles: 0x68 1x1, 0x70 8x8, 0x78 16x16 (no size word). */
    pc_draw_env(0, 0, 32, 32, 0, 0, 0);
    pc_vram_clear(0, 0, 32, 32, 0, 0, 0);
    uint32_t dot[] = { 0, 0x680000ff, XY(1, 1) };
    commands(dot, 2);
    CHECK(read_pixel(1, 1) == 31 && read_pixel(2, 1) == 0 && read_pixel(1, 2) == 0);
    uint32_t tile8[] = { 0, 0x7000ff00, XY(4, 4), 0x780000ff, XY(12, 12) };
    commands(tile8, 4);
    CHECK(read_pixel(4, 4) == 0x3e0 && read_pixel(11, 11) == 0x3e0 && read_pixel(12, 4) == 0);
    CHECK(read_pixel(12, 12) == 31 && read_pixel(27, 27) == 31 && read_pixel(28, 27) == 0);
    puts("PASS: polygon winding, shared-edge blending, Gouraud, texture transparency, masks, "
         "clipping, fixed-size rectangles");
    return 0;
}
