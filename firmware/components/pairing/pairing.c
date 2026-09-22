#include "pairing.h"
#include "board_config.h"
#include "storage.h"
#include "logx.h"
#include "display.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/platform_util.h"

static const char *TAG = "pair";

#define TOKEN_HEX_LEN (CFG_PAIR_TOKEN_BYTES * 2)   /* 64 hex chars */

static pair_state_t s_state = PAIR_STATE_UNPAIRED;

static char    s_otp[7];          /* live only while awaiting confirmation */
static int64_t s_otp_expire_us;
static int     s_otp_attempts;

static uint8_t s_token_hash[32];
static bool    s_token_hash_valid;

static char s_peer_ip[46];

static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;

static void sha256(const void *data, size_t len, uint8_t out[32])
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);            /* 0 = SHA-256, not SHA-224 */
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

static void bytes_to_hex(const uint8_t *in, size_t n, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = hex[in[i] >> 4];
        out[2 * i + 1] = hex[in[i] & 0x0F];
    }
    out[2 * n] = '\0';
}

static void clear_session(void)
{
    /* The OTP is a secret for its whole lifetime; wipe it rather than letting
     * it linger in .bss where a later heap dump or core file could show it. */
    mbedtls_platform_zeroize(s_otp, sizeof(s_otp));
    s_otp_expire_us = 0;
    s_otp_attempts  = 0;
}

void pairing_load(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);

    storage_get_str(NVS_NS_AUTH, "ipad_ip", s_peer_ip, sizeof(s_peer_ip));

    bool paired_flag = storage_get_i32(NVS_NS_AUTH, "paired", 0) == 1;
    s_token_hash_valid = storage_get_blob(NVS_NS_AUTH, "tok_hash",
                                          s_token_hash, sizeof(s_token_hash));

    if (paired_flag && s_token_hash_valid) {
        s_state = PAIR_STATE_PAIRED;
        LOGI(TAG, "device already paired (peer=%s)",
             s_peer_ip[0] ? s_peer_ip : "unknown");
    } else if (paired_flag) {
        /*
         * A unit flashed over the MicroPython firmware looks exactly like
         * this: auth/paired is 1 but there is no bearer-token hash, because
         * the old build stored an AES key instead. Treating it as unpaired is
         * the only honest option — there is no credential to accept.
         */
        LOGW(TAG, "paired flag set but no token hash — treating as unpaired "
                  "(re-pair the iPad)");
        storage_erase_key(NVS_NS_AUTH, "paired");
        s_state = PAIR_STATE_UNPAIRED;
    } else {
        s_state = PAIR_STATE_UNPAIRED;
        LOGI(TAG, "device not paired");
    }
}

pair_state_t pairing_state(void) { return s_state; }
bool pairing_is_paired(void)     { return s_state == PAIR_STATE_PAIRED; }

bool pairing_start(int *expires_in)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_state == PAIR_STATE_PAIRED) {
        xSemaphoreGive(s_lock);
        return false;
    }

    /*
     * Six uniformly distributed digits straight from the hardware RNG. The
     * Python build ran HOTP(RFC 4226) with a random 32-byte secret and a
     * counter fixed at 0, which is an elaborate way of producing exactly this
     * — one random 6-digit code per attempt — so the HMAC machinery is gone.
     */
    uint32_t r = esp_random() % 1000000u;
    snprintf(s_otp, sizeof(s_otp), "%06u", (unsigned)r);

    s_otp_expire_us = esp_timer_get_time() + (int64_t)CFG_PAIR_OTP_TTL_S * 1000000;
    s_otp_attempts  = 0;
    s_state         = PAIR_STATE_AWAITING_OTP;

    xSemaphoreGive(s_lock);

    /* Never logged, at any level, and never returned by the API: the OLED in
     * the room is the only place it appears. */
    display_pairing_otp(s_otp, CFG_PAIR_OTP_TTL_S);
    LOGI(TAG, "pairing started — OTP on display, valid %ds", CFG_PAIR_OTP_TTL_S);

    if (expires_in) *expires_in = CFG_PAIR_OTP_TTL_S;
    return true;
}

bool pairing_confirm(const char *otp, char *token_out, size_t token_len,
                     const char **err)
{
    if (token_len <= TOKEN_HEX_LEN) {
        *err = "internal";
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_state != PAIR_STATE_AWAITING_OTP) {
        xSemaphoreGive(s_lock);
        *err = "not_in_pairing_mode";
        return false;
    }
    if (esp_timer_get_time() > s_otp_expire_us) {
        clear_session();
        s_state = PAIR_STATE_UNPAIRED;
        xSemaphoreGive(s_lock);
        LOGW(TAG, "pairing OTP expired — back to unpaired");
        *err = "otp_expired";
        return false;
    }

    /* Constant-time, and length-checked first so the comparison itself never
     * depends on the attacker-supplied length. */
    bool ok = otp && strlen(otp) == 6 &&
              mbedtls_ct_memcmp(otp, s_otp, 6) == 0;

    if (!ok) {
        s_otp_attempts++;
        int left = CFG_PAIR_OTP_MAX_TRIES - s_otp_attempts;
        if (left <= 0) {
            clear_session();
            s_state = PAIR_STATE_UNPAIRED;
            xSemaphoreGive(s_lock);
            LOGW(TAG, "too many wrong OTPs — session cancelled");
            *err = "too_many_attempts";
            return false;
        }
        xSemaphoreGive(s_lock);
        LOGW(TAG, "wrong OTP (%d attempt(s) left)", left);
        *err = "invalid_otp";
        return false;
    }

    /* 256 bits from the hardware RNG. */
    uint8_t token[CFG_PAIR_TOKEN_BYTES];
    esp_fill_random(token, sizeof(token));
    bytes_to_hex(token, sizeof(token), token_out);

    /* Only the hash is persisted, so reading the flash later yields nothing
     * that can be presented as a credential. */
    sha256(token_out, TOKEN_HEX_LEN, s_token_hash);
    s_token_hash_valid = true;
    mbedtls_platform_zeroize(token, sizeof(token));

    storage_set_blob(NVS_NS_AUTH, "tok_hash", s_token_hash, sizeof(s_token_hash));
    storage_set_i32(NVS_NS_AUTH, "paired", 1);

    clear_session();
    s_state = PAIR_STATE_PAIRED;
    xSemaphoreGive(s_lock);

    /* Deliberately says nothing about the token itself. */
    LOGI(TAG, "pairing complete — bearer token issued");
    display_paired_ok();
    return true;
}

bool pairing_check_token(const char *token)
{
    if (!token || !s_token_hash_valid || s_state != PAIR_STATE_PAIRED) {
        return false;
    }
    size_t len = strlen(token);
    if (len != TOKEN_HEX_LEN) {
        return false;
    }
    uint8_t h[32];
    sha256(token, len, h);
    bool ok = mbedtls_ct_memcmp(h, s_token_hash, sizeof(h)) == 0;
    mbedtls_platform_zeroize(h, sizeof(h));
    return ok;
}

void pairing_unpair(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    storage_erase_key(NVS_NS_AUTH, "tok_hash");
    storage_erase_key(NVS_NS_AUTH, "paired");
    storage_erase_key(NVS_NS_AUTH, "ipad_ip");

    mbedtls_platform_zeroize(s_token_hash, sizeof(s_token_hash));
    s_token_hash_valid = false;
    s_peer_ip[0] = '\0';
    clear_session();
    s_state = PAIR_STATE_UNPAIRED;

    /*
     * A newly re-paired iPad must start with a full reboot budget, otherwise
     * a device that spent its budget on the previous iPad carries that
     * grudge over to the new one.
     */
    storage_set_i32(NVS_NS_WDT, "peerfail", 0);

    xSemaphoreGive(s_lock);
    LOGI(TAG, "device unpaired");
}

bool pairing_get_peer_ip(char *out, size_t len)
{
    if (!s_peer_ip[0]) {
        return false;
    }
    strlcpy(out, s_peer_ip, len);
    return true;
}

void pairing_set_peer_ip(const char *ip)
{
    if (!ip || !ip[0] || strcmp(ip, s_peer_ip) == 0) {
        return;
    }
    strlcpy(s_peer_ip, ip, sizeof(s_peer_ip));
    storage_set_str(NVS_NS_AUTH, "ipad_ip", s_peer_ip);
    LOGI(TAG, "peer address is now %s", s_peer_ip);
}

void pairing_tick(void)
{
    if (s_state != PAIR_STATE_AWAITING_OTP) {
        return;
    }
    if (esp_timer_get_time() > s_otp_expire_us) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_state == PAIR_STATE_AWAITING_OTP &&
            esp_timer_get_time() > s_otp_expire_us) {
            clear_session();
            s_state = PAIR_STATE_UNPAIRED;
            LOGW(TAG, "pairing window expired — back to unpaired");
        }
        xSemaphoreGive(s_lock);
    }
}
