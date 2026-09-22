/*
 * relay.h — the maglock relay.
 *
 * Polarity lives in board_config.h (MAGLOCK_OPEN / MAGLOCK_LOCKED) and is an
 * assumption until somebody measures the real cabinet: COM+NO, so energising
 * the coil opens the door and a board power cut locks it. Every call site says
 * OPEN or LOCKED, never 1 or 0, so reversing the wiring is a two-line change.
 */
#pragma once

#include <stdbool.h>

void relay_init(void);
bool relay_is_ready(void);

/* Drive the maglock. Writing the value it already holds is a no-op at the
 * register level (the GPIO is write-1-to-set), so repeats cost nothing
 * mechanically — but see relay_set_maglock()'s comment about chattering. */
void relay_set_maglock(int value);
int  relay_get_maglock(void);

/*
 * Fail-safe latch. While engaged the door is deliberately held open because
 * the controller can no longer be commanded, so the button task is not allowed
 * to drive it closed. Opening always works — manual egress must never be
 * blocked by firmware state.
 */
void relay_set_failsafe(bool active);
bool relay_failsafe_active(void);

/*
 * There is no button task: PCBA Rev 1.1 has no manual-open button wired to a
 * pin this firmware is allowed to use, and the dry-contact inputs are out of
 * scope. See the comment in board_pins.h.
 */
