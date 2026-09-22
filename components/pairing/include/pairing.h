/*
 * pairing.h — OTP pairing and the bearer token that follows it.
 *
 * This replaces the RSA-2048 + AES-256-GCM + nonce + HOTP stack of the
 * MicroPython firmware (spec v0.1, requirement 10: "drop the security
 * mechanisms that are not needed; keep TLS + token"). What is left:
 *
 *     unpaired --POST /api/pair/start--> awaiting_otp --confirm--> paired
 *
 *   - pair/start shows a 6-digit OTP on the OLED and returns nothing secret.
 *   - pair/confirm checks the OTP and returns a 256-bit bearer token ONCE.
 *     Only the token's SHA-256 is stored, so a later dump of NVS does not
 *     yield a working credential.
 *   - Every other endpoint carries `Authorization: Bearer <token>`.
 *   - While paired, pair/start and pair/confirm answer 409 until unpaired.
 *
 * Three habits carried over from the old code because they were right:
 *   - the OTP is never logged, at any level;
 *   - the token is never logged either — the Python build logged the pairing
 *     session_token in plaintext, where /logs then handed it out;
 *   - comparisons of secrets are constant-time.
 *
 * And one that was wrong and is not carried over: the Python build treated a
 * missing iPad public key as "dev mode" and skipped signature checking
 * entirely. There is no such bypass here.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PAIR_STATE_UNPAIRED = 0,
    PAIR_STATE_AWAITING_OTP,
    PAIR_STATE_PAIRED,
} pair_state_t;

/* Restores state from NVS. A `paired` flag with no token hash behind it is
 * treated as unpaired — that is what a unit migrated from the MicroPython
 * firmware looks like, and pairing again is the correct recovery. */
void pairing_load(void);

pair_state_t pairing_state(void);
bool pairing_is_paired(void);

/*
 * Begin pairing: generates an OTP, shows it on the display and starts the
 * CFG_PAIR_OTP_TTL_S window. Returns false (and changes nothing) if the device
 * is already paired. `expires_in` receives the TTL in seconds.
 */
bool pairing_start(int *expires_in);

/*
 * Finish pairing. On success `token_out` receives the hex bearer token — the
 * only time it exists in the clear — and the caller must hand it straight to
 * the client. Returns false with `err` set to a short machine-readable reason:
 * "not_in_pairing_mode", "otp_expired", "invalid_otp", "too_many_attempts".
 *
 * A wrong OTP costs one of CFG_PAIR_OTP_MAX_TRIES attempts; spending them all
 * burns the session, which then has to be restarted from pair/start.
 */
bool pairing_confirm(const char *otp, char *token_out, size_t token_len,
                     const char **err);

/* Constant-time check of a presented bearer token against the stored hash. */
bool pairing_check_token(const char *token);

/* Clears the token hash, the paired flag and the stored peer address. */
void pairing_unpair(void);

/* Address the paired iPad last talked to us from, for the ping watchdog. */
bool pairing_get_peer_ip(char *out, size_t len);
void pairing_set_peer_ip(const char *ip);

/* Called on the display task's tick: expires an unfinished pairing session. */
void pairing_tick(void);
