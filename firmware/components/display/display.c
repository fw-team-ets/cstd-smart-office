#include "display.h"
#include "font6x8.h"
#include "board_pins.h"
#include "board_config.h"
#include "i2cbus.h"
#include "logx.h"
#include "supervisor.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static const char *TAG = "lcd";

#define OLED_W      128
#define OLED_H      64
#define OLED_PAGES  (OLED_H / 8)
#define LINE_PITCH  10          /* px between text rows */
#define BOTTOM_Y    56          /* baseline row for the version string */

static bool s_ready;
static uint8_t s_fb[OLED_PAGES * OLED_W];

/* 0x40 control byte + the whole framebuffer: 1025 bytes in one transfer.
 * Pre-allocated because this is sent on every redraw and the heap is scarce. */
static uint8_t s_out[1 + sizeof(s_fb)];

static char s_idle[DISPLAY_LINES][DISPLAY_COLS + 1];
static char s_idle_bottom[DISPLAY_COLS + 1];
static bool s_idle_valid;

static int64_t s_expire_us;      /* 0 = nothing pending */

static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;

/* ── Panel primitives ────────────────────────────────────────────────────── */

static esp_err_t oled_cmd(uint8_t c)
{
    const uint8_t buf[2] = { 0x80, c };   /* Co=1, D/C=0 */
    return i2cbus_write(BOARD_SSD1306_ADDR, buf, sizeof(buf));
}

static esp_err_t oled_cmds(const uint8_t *cmds, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        esp_err_t err = oled_cmd(cmds[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static esp_err_t oled_init_panel(void)
{
    static const uint8_t init[] = {
        0xAE,             /* display off */
        0xD5, 0x80,       /* clock divide / oscillator frequency */
        0xA8, OLED_H - 1, /* multiplex ratio */
        0xD3, 0x00,       /* display offset */
        0x40,             /* start line 0 */
        0x8D, 0x14,       /* charge pump on */
        0x20, 0x00,       /* horizontal addressing mode */
        0xA1,             /* segment remap: column 127 -> SEG0 */
        0xC8,             /* COM scan remapped */
        0xDA, 0x12,       /* COM pins config for 128x64 */
        0x81, 0xCF,       /* contrast */
        0xD9, 0xF1,       /* pre-charge */
        0xDB, 0x40,       /* VCOMH deselect */
        0xA4,             /* output follows RAM */
        0xA6,             /* non-inverted */
        0xAF,             /* display on */
    };
    return oled_cmds(init, sizeof(init));
}

/*
 * Push the whole framebuffer. ~25 ms at 400 kHz hardware I2C, against ~92 ms
 * of fully blocked CPU on the MicroPython SoftI2C build — which is why this
 * lives in its own task rather than on the caller's stack.
 *
 * The caller must hold the I2C lock: the address-window commands and the data
 * burst are one sequence, and a TPS25751 4CC poll landing in the middle of it
 * would leave the panel writing pixels at the wrong address.
 */
static esp_err_t oled_flush_locked(void)
{
    const uint8_t win[] = {
        0x21, 0, OLED_W - 1,        /* column address range */
        0x22, 0, OLED_PAGES - 1,    /* page address range */
    };
    esp_err_t err = oled_cmds(win, sizeof(win));
    if (err != ESP_OK) return err;

    s_out[0] = 0x40;                /* Co=0, D/C=1 -> data stream */
    memcpy(&s_out[1], s_fb, sizeof(s_fb));
    return i2cbus_write(BOARD_SSD1306_ADDR, s_out, sizeof(s_out));
}

/* ── Framebuffer drawing ─────────────────────────────────────────────────── */

static void fb_clear(void) { memset(s_fb, 0, sizeof(s_fb)); }

/*
 * Draw one 8-row glyph column at (x, y). y is NOT page-aligned here — the text
 * rows sit at 0, 10, 20, 30, 40 — so each column straddles two pages and has
 * to be shifted into both.
 */
static void fb_column(int x, int y, uint8_t bits)
{
    if (x < 0 || x >= OLED_W || y >= OLED_H) return;

    int page  = y / 8;
    int shift = y % 8;

    if (page >= 0 && page < OLED_PAGES) {
        s_fb[page * OLED_W + x] |= (uint8_t)(bits << shift);
    }
    if (shift && (page + 1) < OLED_PAGES) {
        s_fb[(page + 1) * OLED_W + x] |= (uint8_t)(bits >> (8 - shift));
    }
}

static void fb_text(const char *s, int x, int y)
{
    for (; *s && x < OLED_W; s++, x += FONT_WIDTH) {
        unsigned char c = (unsigned char)*s;
        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) {
            c = '?';
        }
        const uint8_t *g = font6x8[c - FONT_FIRST_CHAR];
        for (int col = 0; col < FONT_WIDTH; col++) {
            fb_column(x + col, y, g[col]);
        }
    }
}

static void render_locked(const char *lines[DISPLAY_LINES], const char *bottom_right)
{
    fb_clear();
    for (int i = 0; i < DISPLAY_LINES; i++) {
        if (lines[i] && lines[i][0]) {
            fb_text(lines[i], 0, i * LINE_PITCH);
        }
    }
    if (bottom_right && bottom_right[0]) {
        int px = OLED_W - (int)strlen(bottom_right) * FONT_WIDTH;
        if (px < 0) px = 0;
        fb_text(bottom_right, px, BOTTOM_Y);
    }
}

/* Draw + flush, taking both locks in a fixed order (display then I2C) so the
 * two can never deadlock against each other. */
static void paint(const char *lines[DISPLAY_LINES], const char *bottom_right)
{
    if (!s_ready) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    render_locked(lines, bottom_right);

    if (i2cbus_lock(pdMS_TO_TICKS(1000))) {
        esp_err_t err = oled_flush_locked();
        i2cbus_unlock();
        if (err != ESP_OK) {
            LOGW(TAG, "flush failed: %s", esp_err_to_name(err));
        }
    } else {
        LOGW(TAG, "flush skipped — I2C bus busy");
    }
    xSemaphoreGive(s_lock);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void display_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);

    /*
     * Three scans 100 ms apart: the panel needs a moment after power-up, and
     * an absent panel must not stall or fail the boot.
     */
    bool found = false;
    for (int i = 0; i < 3 && !found; i++) {
        found = i2cbus_probe(BOARD_SSD1306_ADDR);
        if (!found) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!found) {
        LOGE(TAG, "SSD1306 not found at 0x%02X — display disabled",
             BOARD_SSD1306_ADDR);
        return;
    }

    if (!i2cbus_lock(pdMS_TO_TICKS(1000))) {
        LOGE(TAG, "init: I2C bus unavailable — display disabled");
        return;
    }
    esp_err_t err = oled_init_panel();
    i2cbus_unlock();

    if (err != ESP_OK) {
        LOGE(TAG, "panel init failed: %s — display disabled", esp_err_to_name(err));
        return;
    }

    s_ready = true;
    LOGI(TAG, "init: addr=0x%02X %dx%d", BOARD_SSD1306_ADDR, OLED_W, OLED_H);
}

bool display_is_ready(void) { return s_ready; }

void display_set_idle(const char *lines[DISPLAY_LINES], const char *bottom_right)
{
    for (int i = 0; i < DISPLAY_LINES; i++) {
        strlcpy(s_idle[i], lines[i] ? lines[i] : "", sizeof(s_idle[i]));
    }
    strlcpy(s_idle_bottom, bottom_right ? bottom_right : "", sizeof(s_idle_bottom));
    s_idle_valid = true;
    s_expire_us  = 0;           /* the idle screen never expires */

    const char *ptrs[DISPLAY_LINES];
    for (int i = 0; i < DISPLAY_LINES; i++) ptrs[i] = s_idle[i];
    paint(ptrs, s_idle_bottom);
}

void display_show(const char *lines[DISPLAY_LINES], int timeout_s)
{
    paint(lines, NULL);
    s_expire_us = (timeout_s > 0)
                      ? esp_timer_get_time() + (int64_t)timeout_s * 1000000
                      : 0;
}

static void restore_idle(void)
{
    s_expire_us = 0;
    if (!s_idle_valid) return;
    const char *ptrs[DISPLAY_LINES];
    for (int i = 0; i < DISPLAY_LINES; i++) ptrs[i] = s_idle[i];
    paint(ptrs, s_idle_bottom);
}

void display_idle_network(const char *ip, const char *mac)
{
    char l1[DISPLAY_COLS + 1], l2[DISPLAY_COLS + 1], l3[DISPLAY_COLS + 1];
    char ver[DISPLAY_COLS + 1];

    snprintf(l1, sizeof(l1), " NETWORK  OK");
    snprintf(l2, sizeof(l2), " %s", ip ? ip : "0.0.0.0");

    /* "MAC " + 12 uppercase hex = exactly 16 characters; the colons in
     * aa:bb:cc:dd:ee:ff would make 17 and clip the last digit. */
    char hex[13] = {0};
    int  n = 0;
    for (const char *p = mac ? mac : ""; *p && n < 12; p++) {
        if (*p != ':') {
            hex[n++] = (*p >= 'a' && *p <= 'f') ? (char)(*p - 'a' + 'A') : *p;
        }
    }
    snprintf(l3, sizeof(l3), "MAC %s", hex);
    snprintf(ver, sizeof(ver), "v%s", FW_VERSION);

    const char *lines[DISPLAY_LINES] = { "", l1, l2, l3, "" };
    display_set_idle(lines, ver);
}

void display_no_network(void)
{
    const char *lines[DISPLAY_LINES] = { "", "  NO NETWORK", "  CHECK CABLE", "", "" };
    display_set_idle(lines, NULL);
}

void display_connecting(void)
{
    const char *lines[DISPLAY_LINES] = { "", "  CONNECTING", "   NETWORK...", "", "" };
    display_set_idle(lines, NULL);
}

void display_pairing_otp(const char *otp, int timeout_s)
{
    char code[DISPLAY_COLS + 1];
    snprintf(code, sizeof(code), "    %s", otp ? otp : "------");

    const char *lines[DISPLAY_LINES] = {
        " PAIRING OTP", "", code, "", " Enter on iPad"
    };
    display_show(lines, timeout_s);
}

void display_paired_ok(void)
{
    const char *lines[DISPLAY_LINES] = { "", "  PAIRED OK!", "  DOOR LOCKED", "", "" };
    display_show(lines, 5);
}

void display_ipad_offline(void)
{
    const char *lines[DISPLAY_LINES] = { "", " IPAD OFFLINE", "", "", "" };
    display_show(lines, 10);
}

static void display_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        supervisor_beat("lcd", 15000);

        if (s_expire_us != 0 && esp_timer_get_time() >= s_expire_us) {
            restore_idle();
        }
    }
}

void display_start_task(void)
{
    xTaskCreate(display_task, "display", 4096, NULL, 4, NULL);
}
