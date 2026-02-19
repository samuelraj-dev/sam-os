#include "display.h"
#include "font8x8_basic.h"

// shadow buffer — declare as a large static array
// 1920 * 1080 * 4 = ~8MB — needs to be allocated properly
// for now use a fixed size that covers common resolutions
#define MAX_FB_SIZE (1920 * 1200 * 4)

// static uint8_t shadow[MAX_FB_SIZE] __attribute__((aligned(4096)));
// static uint32_t* sb = (uint32_t*)shadow;  // shadow buffer

static uint32_t* sb = 0;

static uint32_t* fb;
static uint32_t  screen_w, screen_h, pitch;
static uint32_t  cursor_x = 0;
static uint32_t  cursor_y = 0;
static uint32_t  fg_color = 0x00FFFFFF;
static uint32_t  bg_color = 0x00303030;

void display_set_shadow(uint32_t* buffer) {
    sb = buffer;
}

void display_flush(void)
{
    if (!sb || !fb) return;  // guard

    uint64_t total_bytes = screen_h * pitch * 4;
    uint64_t* src = (uint64_t*)sb;
    uint64_t* dst = (uint64_t*)fb;

    for (uint64_t i = 0; i < total_bytes / 8; i++)
        dst[i] = src[i];
}

static void scroll(void)
{
    // copy rows up — use pitch for correct row stride
    uint64_t row_bytes = pitch * 4;
    uint8_t* src = (uint8_t*)sb + 10 * row_bytes;
    uint8_t* dst = (uint8_t*)sb;
    uint64_t copy_bytes = (screen_h - 10) * row_bytes;

    // copy as uint64 for speed
    uint64_t* s64 = (uint64_t*)src;
    uint64_t* d64 = (uint64_t*)dst;
    for (uint64_t i = 0; i < copy_bytes / 8; i++)
        d64[i] = s64[i];

    // clear bottom 10 rows
    for (uint32_t y = screen_h - 10; y < screen_h; y++)
        for (uint32_t x = 0; x < screen_w; x++)
            sb[y * pitch + x] = bg_color;

    cursor_y -= 10;
}

void display_init(uint32_t* framebuffer, uint32_t width,
                  uint32_t height, uint32_t pixels_per_scanline)
{
    fb       = framebuffer;
    screen_w = width;
    screen_h = height;
    pitch    = pixels_per_scanline;
    cursor_x = 0;
    cursor_y = 0;
}

uint32_t display_get_height(void) { return screen_h; }
uint32_t display_get_pitch(void)  { return pitch; }

void clear_screen() {
    for (uint32_t y = 0; y < screen_h; y++)
        for (uint32_t x = 0; x < screen_w; x++)
            fb[y * pitch + x] = bg_color;
}

// void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
//     fb[y * pitch + x] = color;
// }
static void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (sb) sb[y * pitch + x] = color;
    else    fb[y * pitch + x] = color;
}

void draw_char(char c, uint32_t x, uint32_t y) {
    // uint8_t* glyph = font8x8_basic[(uint8_t)c];
    unsigned char* glyph = font8x8_basic[(uint8_t)c];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (1 << col))
                put_pixel(x + col, y + row, fg_color);
            else
                put_pixel(x + col, y + row, bg_color);
        }
    }
}

// void print_char(char c) {
//     if (c == '\n') {
//         cursor_x = 0;
//         cursor_y += 10;
//         return;
//     }

//     draw_char(c, cursor_x, cursor_y);
//     cursor_x += 8;

//     if (cursor_x + 8 >= screen_w) {
//         cursor_x = 0;
//         cursor_y += 10;
//     }
// }
void print_char(char c)
{
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += 10;
        if (cursor_y + 10 >= screen_h) scroll();
        return;
    }

    draw_char(c, cursor_x, cursor_y);
    cursor_x += 8;

    if (cursor_x + 8 >= screen_w) {
        cursor_x = 0;
        cursor_y += 10;
        if (cursor_y + 10 >= screen_h) scroll();
    }
}

void print(const char* str) {
    while (*str)
        print_char(*str++);
}

void print_hex(uint64_t val) {
    char hex[] = "0123456789ABCDEF";
    print("0x");
    for (int i = 60; i >= 0; i -= 4)
        print_char(hex[(val >> i) & 0xF]);
}

void print_dec(uint64_t val) {
    char buf[32];
    int i = 0;

    if (val == 0) {
        print("0");
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    for (int j = i - 1; j >= 0; j--)
        print_char(buf[j]);
}
