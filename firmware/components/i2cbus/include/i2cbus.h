/*
 * i2cbus.h — the shared I2C bus, and the lock that makes it safe.
 *
 * Two devices share GPIO1/GPIO2: the TPS25751 PD controller (0x20) and the
 * SSD1306 OLED (0x3C). The MicroPython firmware had no lock at all and got
 * away with it because asyncio is cooperative and no `await` ever sat between
 * the two halves of an I2C transaction. FreeRTOS preempts, so that argument
 * is gone.
 *
 * Per-transfer locking is NOT enough. A 4CC task on the TPS25751 is "write the
 * command, then poll CMD_1 until it clears" — if the display task slips a
 * 1025-byte frame flush between those two steps the poll can read a register
 * the PD controller was still updating. Callers therefore take the bus around
 * the whole SEQUENCE:
 *
 *     i2cbus_lock(portMAX_DELAY);
 *     ... write 4CC, poll CMD_1, read DATA_1 ...
 *     i2cbus_unlock();
 *
 * i2cbus_write()/i2cbus_read() do NOT lock on their own. That is the point:
 * a function that locks internally cannot be composed into a larger atomic
 * sequence.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t i2cbus_init(void);

bool i2cbus_lock(TickType_t timeout_ticks);
void i2cbus_unlock(void);

/* Raw transfers. The caller must already hold the lock. */
esp_err_t i2cbus_write(uint8_t addr, const uint8_t *data, size_t len);
esp_err_t i2cbus_read(uint8_t addr, uint8_t *data, size_t len);
esp_err_t i2cbus_write_read(uint8_t addr,
                            const uint8_t *wr, size_t wr_len,
                            uint8_t *rd, size_t rd_len);

/* True if a device ACKs at `addr`. Takes the lock itself — it is a single
 * transaction with nothing to compose. */
bool i2cbus_probe(uint8_t addr);
