/*
 * display.h — SSD1306 128x64 OLED on the shared I2C bus at 0x3C.
 *
 * Five text rows, 10 px apart, 16 characters wide (8 px font cell). Every
 * screen in this firmware is built to that grid, which is why the idle screen
 * strips the colons out of the MAC: "3c:0f:02:d7:ea:c3" is 17 characters and
 * the last one would fall off the right edge, while "MAC " + 12 hex digits is
 * exactly 16.
 *
 * A missing panel is not a boot failure. If nothing ACKs at 0x3C after three
 * scans the display is simply marked absent and every call here becomes a
 * no-op — a controller with a dead OLED still has to open the door.
 */
#pragma once

#include <stdbool.h>

#define DISPLAY_LINES     5
#define DISPLAY_COLS      16

void display_init(void);
bool display_is_ready(void);

/*
 * Set the persistent screen. This is what the display falls back to when a
 * timed notification expires. `bottom_right` is drawn right-aligned at y=56
 * (the firmware version on the idle screen); pass NULL for none.
 */
void display_set_idle(const char *lines[DISPLAY_LINES], const char *bottom_right);

/* Show `lines` for `timeout_s` seconds, then fall back to the idle screen.
 * timeout_s = 0 shows them until something else replaces them. */
void display_show(const char *lines[DISPLAY_LINES], int timeout_s);

/* Convenience screens, so that no two call sites can word them differently. */
void display_idle_network(const char *ip, const char *mac);
void display_no_network(void);
void display_connecting(void);
void display_pairing_otp(const char *otp, int timeout_s);
void display_paired_ok(void);
void display_ipad_offline(void);

/* Starts the task that expires timed notifications (2 s tick). */
void display_start_task(void);
