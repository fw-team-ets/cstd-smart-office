/*
 * board_pins.h — the ONLY place GPIO numbers are written down.
 *
 * PCBA 9261 Rev 1.1, ESP32-S3-ETH carrier. Every number here is taken from the
 * pin table in the port specification (section 3.2) and the PCBA document; do
 * not copy any of them into another file, include this header instead.
 */
#pragma once

#include "driver/gpio.h"

/* ── Shared I2C bus ──────────────────────────────────────────────────────────
 * One bus, two devices: the TPS25751 PD controller at 0x20 and the SSD1306
 * OLED at 0x3C. The MicroPython build got away without a lock because asyncio
 * never preempted a transaction; FreeRTOS does, so every multi-step sequence
 * on this bus goes through the i2cbus mutex (see components/i2cbus).
 */
#define BOARD_I2C_SCL_GPIO      GPIO_NUM_1
#define BOARD_I2C_SDA_GPIO      GPIO_NUM_2
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_I2C_FREQ_HZ       400000   /* hardware I2C: ~25 ms per OLED flush
                                          * vs ~92 ms of blocked CPU on SoftI2C */

#define BOARD_TPS25751_ADDR     0x20     /* ADDR0 = ADDR1 = 0 */
#define BOARD_SSD1306_ADDR      0x3C

/* ── W5500 Ethernet (SPI) ─────────────────────────────────────────────────── */
#define BOARD_W5500_RST_GPIO    GPIO_NUM_9    /* active low, hold >= 500 ms */
#define BOARD_W5500_INT_GPIO    GPIO_NUM_10
#define BOARD_W5500_MOSI_GPIO   GPIO_NUM_11
#define BOARD_W5500_MISO_GPIO   GPIO_NUM_12
#define BOARD_W5500_SCLK_GPIO   GPIO_NUM_13
#define BOARD_W5500_CS_GPIO     GPIO_NUM_14
#define BOARD_W5500_SPI_HOST    SPI2_HOST
#define BOARD_W5500_SPI_HZ      10000000      /* 10 MHz, as shipped. Raising it
                                               * needs a stability soak first. */

/* ── Maglock relay ───────────────────────────────────────────────────────── */
#define BOARD_RELAY_MAGLOCK_GPIO GPIO_NUM_40  /* RELAY_01. RELAY_02 (GPIO39) is
                                               * not used in this build. */

/*
 * There is NO manual-open button in this firmware, and that is a decision, not
 * an omission.
 *
 * The MicroPython config had button_open = GPIO44, but on PCBA 9261 Rev 1.1
 * GPIO43/44 are ESP_TX0/ESP_RX0 — UART0, used for flashing and the log
 * console. The only physical button on that board is BTN3 on GPIO38
 * (TEST_BTN, R35 10k pull-up, C47 0.1uF debounce), and the dry-contact inputs
 * DIN_01/DIN_02 (GPIO42/41) are the header a real request-to-exit button would
 * land on. The approved scope (spec v0.1 section 3.4) drops GPIO38, GPIO42 and
 * GPIO41, so none of them is driven here and GPIO44 stays with UART0.
 *
 * Operational consequence, recorded in Checklist.txt: with no button and no
 * DIN, the door can only be opened over the network. If the cabinet has no
 * parallel hardware release (REX, break-glass, fire panel), this firmware is
 * the single point of failure for egress. That is a hardware question, not a
 * firmware one, and it is still open.
 */

/* ── Pins that must never be driven ───────────────────────────────────────── */
/*
 * GPIO36 — octal PSRAM SPI D4, and loaded by the 5 V regulator EN circuit on
 * the mainboard. Driving it (directly, or indirectly by enabling PSRAM) hangs
 * the stage-2 bootloader. Nothing in this firmware may configure it.
 */
#define BOARD_FORBIDDEN_GPIO    36

/*
 * ESP32-S3 strapping pins: 0, 3, 45, 46.
 * Free and safe if more I/O is ever needed: 21, 38, 39, 41, 42, 47, 48.
 *
 * RS485 (GPIO 5, 6, 16, 17, 18) is not used by this firmware. The pins are
 * left in their reset state (input, no pull) rather than being configured —
 * see board_idle_unused_pins().
 */
