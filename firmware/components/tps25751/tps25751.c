#include "tps25751.h"
#include "board_pins.h"
#include "board_config.h"
#include "i2cbus.h"
#include "logx.h"
#include "storage.h"
#include "supervisor.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "pd";

/* ── Register map (TRM SLVUCR8A) ─────────────────────────────────────────── */
#define REG_MODE           0x03   /* 4  ASCII device mode */
#define REG_TYPE_C_STATUS  0x05   /* 5  Type-C connection status */
#define REG_CMD_1          0x08   /* 4  4CC command */
#define REG_DATA_1         0x09   /* var 4CC data / task return code */
#define REG_INT_EVENT      0x14   /* 11 */
#define REG_INT_MASK       0x16   /* 11 */
#define REG_INT_CLEAR      0x18   /* 11 */
#define REG_STATUS         0x1A   /* 5  plug, roles, VBUS */
#define REG_PORT_CONFIG    0x28   /* 16 port configuration */
#define REG_BOOT_FLAGS     0x2D   /* 5  boot flags — NOT a version register */
#define REG_ACTIVE_PDO     0x34   /* 6  active contract */
#define REG_PD_STATUS      0x40   /* 4  negotiated PD status */

/* TYPE_C_STATUS (0x05) byte 0 */
#define BIT_CC_CONNECTED   0x01
#define BIT_CURRENT_1A5    0x04
#define BIT_CURRENT_3A     0x08

/* STATUS (0x1A) byte 0 */
#define BIT_PLUG_PRESENT   0x01
#define BIT_PORT_ROLE_SRC  0x20   /* bit 5: CC terminations, 1 = Source */
#define BIT_DATA_ROLE_DFP  0x40   /* bit 6: 0 = UFP, 1 = DFP */
/* STATUS bits [21:20] = VBUS status -> byte 2 bits [5:4] */
#define VBUS_SAFE0V        0      /* < 0.8 V */
#define VBUS_5V            1      /* 4.75 - 5.5 V */
#define VBUS_IN_RANGE      2      /* within the contract's expected limits */

/* PD_STATUS (0x40) byte 0 bit 6 = PresentRole.
 * Reset value is 0 (Sink), so this reads "sink" both when PD genuinely
 * negotiated Sink and when there is no contract at all. STATUS bit 5 plus the
 * VBUS measurement is the honest answer to "is the iPad being fed". */
#define BIT_PD_POWER_SRC   0x40

/* PORT_CONFIG (0x28) byte 0 bits[1:0] — TRM 3.10 */
#define PORT_TYPE_MASK     0x03
#define PORT_TYPE_SINK     0x00
#define PORT_TYPE_SOURCE   0x01
#define PORT_TYPE_DRP      0x02   /* also the register's reset default */
#define PORT_TYPE_DISABLED 0x03
#define BIT_INIT_DATA_DFP  0x20   /* bit 5: initial data role, 0 = UFP */

/* BOOT_FLAGS (0x2D) — TRM 3.12 */
#define BOOT_PATCH_HDR_ERR    0x01   /* byte0 bit0 */
#define BOOT_DEAD_BATTERY     0x04   /* byte0 bit2 */
#define BOOT_EEPROM_PRESENT   0x08   /* byte0 bit3 */
#define BOOT_REGION0_ATTEMPT  0x10   /* byte0 bit4 */
#define BOOT_REGION1_ATTEMPT  0x20   /* byte0 bit5 */
#define BOOT_REGION0_INVALID  0x40   /* byte0 bit6 */
#define BOOT_REGION1_INVALID  0x80   /* byte0 bit7 */
#define BOOT_REGION0_EE_ERR   0x01   /* byte1 bit0 */
#define BOOT_REGION1_EE_ERR   0x02   /* byte1 bit1 */
#define BOOT_PATCH_DL_ERR     0x04   /* byte1 bit2 */
#define BOOT_REGION0_CRC_FAIL 0x10   /* byte1 bit4 */
#define BOOT_REGION1_CRC_FAIL 0x20   /* byte1 bit5 */
/* Patch Config Source: byte3 bits[7:5] */
#define CFG_SRC_NONE   0
#define CFG_SRC_EEPROM 5
#define CFG_SRC_I2C    6

/* INT_EVENT (0x14) — 11-byte little-endian bitfield */
#define INT_PLUG_CHANGE   (1ULL << 3)
#define INT_POWER_SWAP    (1ULL << 4)
#define INT_DATA_SWAP     (1ULL << 5)
#define INT_OVERCURRENT   (1ULL << 9)
#define INT_INCOMPATIBLE  (1ULL << 32)
#define INT_PROTOCOL_ERR  (1ULL << 38)
#define INT_MASK_ALL (INT_PLUG_CHANGE | INT_POWER_SWAP | INT_DATA_SWAP | \
                      INT_OVERCURRENT | INT_INCOMPATIBLE | INT_PROTOCOL_ERR)

/* 4CC tasks. 'Gaid', 'SWSr' and 'SWSk' are deliberately absent — see the
 * header. 'DBfg' clears the dead-battery flag and produces no output data. */
#define CMD_DBFG "DBfg"

#define PORT_SETTLE_MS 1500   /* port disconnect + reconnect after a write */

static bool s_ready;
static bool s_busy;                /* a port-role change is in flight */
static char s_mode_cache[8] = "?";
static char s_charge_cache[4] = "?";

/* Serialises port-role changes. Separate from the I2C bus lock: it is held
 * across the whole ~1.5 s settle, and holding the bus that long would starve
 * the display. */
static SemaphoreHandle_t s_op_lock;
static StaticSemaphore_t s_op_lock_buf;

/* ── Length-prefixed register access (TRM, Host Interface) ───────────────────
 *   Read:  write [reg]              ; read [len, data0..dataN]  -> strip len
 *   Write: write [reg, len, data0..dataN]
 * The caller must already hold the I2C bus lock.
 */
static esp_err_t reg_read(uint8_t reg, uint8_t *out, size_t len)
{
    uint8_t buf[20];
    if (len + 1 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = i2cbus_write_read(BOARD_TPS25751_ADDR, &reg, 1, buf, len + 1);
    if (err != ESP_OK) {
        return err;
    }
    memcpy(out, &buf[1], len);   /* byte 0 is the length prefix */
    return ESP_OK;
}

static esp_err_t reg_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[20];
    if (len + 2 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = reg;
    buf[1] = (uint8_t)len;
    memcpy(&buf[2], data, len);
    return i2cbus_write(BOARD_TPS25751_ADDR, buf, len + 2);
}

/* Convenience wrappers that take the bus for a single transaction. */
static esp_err_t reg_read_locked(uint8_t reg, uint8_t *out, size_t len)
{
    if (!i2cbus_lock(pdMS_TO_TICKS(1000))) return ESP_ERR_TIMEOUT;
    esp_err_t err = reg_read(reg, out, len);
    i2cbus_unlock();
    return err;
}

/*
 * Retry a read through the EEPROM-load NAK window. The TPS25751 NAKs I2C while
 * it is loading its configuration; without this, that NAK surfaces as a plain
 * error out of init() and abandons the bounded APP-mode retry entirely instead
 * of just waiting the window out.
 */
static esp_err_t reg_read_retry(uint8_t reg, uint8_t *out, size_t len)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 10; i++) {
        err = reg_read_locked(reg, out, len);
        if (err == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return err;
}

/* ── BOOT_FLAGS decoding ─────────────────────────────────────────────────── */

static const char *cfg_src_name(uint8_t code, char *scratch, size_t n)
{
    switch (code) {
    case CFG_SRC_NONE:   return "none";
    case CFG_SRC_EEPROM: return "eeprom";
    case CFG_SRC_I2C:    return "i2c";
    default:
        snprintf(scratch, n, "%u", code);
        return scratch;
    }
}

static void boot_flags_summary(const uint8_t b[5], char *out, size_t n)
{
    char errs[128] = {0};
    size_t len = 0;

    #define ADD_ERR(cond, name) \
        do { if (cond) { \
            len += snprintf(errs + len, sizeof(errs) - len, \
                            len ? ",%s" : "%s", name); } } while (0)

    ADD_ERR(b[0] & BOOT_PATCH_HDR_ERR,    "patch_header_err");
    ADD_ERR(b[0] & BOOT_REGION0_INVALID,  "region0_invalid");
    ADD_ERR(b[0] & BOOT_REGION1_INVALID,  "region1_invalid");
    ADD_ERR(b[1] & BOOT_REGION0_EE_ERR,   "region0_ee_err");
    ADD_ERR(b[1] & BOOT_REGION1_EE_ERR,   "region1_ee_err");
    ADD_ERR(b[1] & BOOT_PATCH_DL_ERR,     "patch_dl_err");
    ADD_ERR(b[1] & BOOT_REGION0_CRC_FAIL, "region0_crc_fail");
    ADD_ERR(b[1] & BOOT_REGION1_CRC_FAIL, "region1_crc_fail");
    #undef ADD_ERR

    char scratch[8];
    snprintf(out, n, "rev=0x%02X cfg_src=%s eeprom=%d dead_battery=%d errors=%s",
             b[4],
             cfg_src_name((b[3] >> 5) & 0x07, scratch, sizeof(scratch)),
             (b[0] & BOOT_EEPROM_PRESENT) ? 1 : 0,
             (b[0] & BOOT_DEAD_BATTERY)   ? 1 : 0,
             errs[0] ? errs : "none");
}

/* ── 4CC tasks ───────────────────────────────────────────────────────────── */

/*
 * Write a 4CC task and poll CMD_1 until the IC clears it. Up to 10 s
 * (200 polls x 50 ms). "!CMD" means the task is not valid in the current
 * state and there is no point waiting for the timeout.
 *
 * CMD_1 clearing means COMPLETED, which per the TRM includes "completed by
 * being rejected". For tasks that produce output the real outcome is the
 * Standard Task Return Code in DATA_1 byte 0; pass check_return for those.
 * 'DBfg' has no output data and leaves stale bytes there, so it does not.
 *
 * The whole sequence — the write, every poll, the return-code read — runs
 * under one bus lock. Splitting it would let an OLED flush land between the
 * command and its poll.
 */
static esp_err_t send_4cc(const char *cmd, const uint8_t *data, size_t data_len,
                          bool check_return)
{
    uint8_t payload[8];
    if (4 + data_len > sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(payload, cmd, 4);
    if (data_len) memcpy(&payload[4], data, data_len);

    /*
     * The bus is held for the WHOLE sequence — write CMD_1, poll it, read the
     * return code — not per transfer. Per-transfer locking would let an OLED
     * frame flush land between the command and its poll, and the specification
     * calls this out explicitly: the lock must span the multi-step sequence.
     *
     * Holding the bus for up to 10 s is acceptable here only because the sole
     * 4CC this firmware sends is 'DBfg' during tps_init(), which runs before
     * display_start_task() and before any other task exists. If a 4CC is ever
     * issued at runtime, give the display a way to skip its flush rather than
     * shortening this lock.
     */
    if (!i2cbus_lock(pdMS_TO_TICKS(2000))) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = reg_write(REG_CMD_1, payload, 4 + data_len);
    if (err != ESP_OK) {
        i2cbus_unlock();
        LOGE(TAG, "4cc %s: write failed: %s", cmd, esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < 200; i++) {
        uint8_t val[4];
        err = reg_read(REG_CMD_1, val, 4);
        if (err == ESP_OK) {
            if (val[0] == 0 && val[1] == 0 && val[2] == 0 && val[3] == 0) {
                /*
                 * Cleared means COMPLETED, which per the TRM includes
                 * "completed by being rejected". For a task that produces
                 * output the real outcome is the Standard Task Return Code in
                 * DATA_1 byte 0. Nothing in the Python driver read it, so a
                 * refused task was indistinguishable from a successful one and
                 * the only trace was a port role that never changed.
                 */
                if (check_return) {
                    uint8_t ret = 0;
                    if (reg_read(REG_DATA_1, &ret, 1) == ESP_OK && ret != 0) {
                        i2cbus_unlock();
                        LOGE(TAG, "4cc %s: task return code 0x%02X", cmd, ret);
                        return ESP_FAIL;
                    }
                }
                i2cbus_unlock();
                LOGD(TAG, "4cc %s: cleared after %d poll(s)", cmd, i + 1);
                return ESP_OK;
            }
            if (memcmp(val, "!CMD", 4) == 0) {
                /* Not valid in the current state: no point waiting out the
                 * remaining timeout. */
                i2cbus_unlock();
                LOGE(TAG, "4cc %s: rejected by IC (!CMD)", cmd);
                return ESP_ERR_INVALID_STATE;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    i2cbus_unlock();
    LOGE(TAG, "4cc %s: timeout after 10s", cmd);
    return ESP_ERR_TIMEOUT;
}

/* ── PDO decoding ────────────────────────────────────────────────────────── */

/*
 * USB-PD Fixed Supply PDO (bits 31:30 == 00):
 *   bits 19:10 = voltage in 50 mV units
 *   bits  9:0  = maximum current in 10 mA units
 * Battery / Variable / APDO layouts differ and are reported as unknown rather
 * than mis-decoded. The Python build reported a flat 20 W whenever any
 * contract existed, so a 5 V / 3 A contract was announced to the iPad as 20 W.
 */
static void decode_pdo(const uint8_t pdo[6], int *mv, int *ma)
{
    uint32_t raw = (uint32_t)pdo[0] | ((uint32_t)pdo[1] << 8) |
                   ((uint32_t)pdo[2] << 16) | ((uint32_t)pdo[3] << 24);
    if (raw == 0 || (raw >> 30) != 0) {
        *mv = 0;
        *ma = 0;
        return;
    }
    *mv = (int)((raw >> 10) & 0x3FF) * 50;
    *ma = (int)(raw & 0x3FF) * 10;
}

/* ── Port role ───────────────────────────────────────────────────────────── */

static esp_err_t port_state(bool *sourcing, int *vbus)
{
    uint8_t st[5];
    esp_err_t err = reg_read_locked(REG_STATUS, st, sizeof(st));
    if (err != ESP_OK) return err;
    *sourcing = (st[0] & BIT_PORT_ROLE_SRC) != 0;
    *vbus     = (st[2] >> 4) & 0x03;
    return ESP_OK;
}

/*
 * Switch the Type-C state machine role by rewriting PORT_CONFIG bits[1:0].
 * TRM 3.10: any modification "will cause a port disconnect and reconnect with
 * the new settings" — precisely the transition wanted, and it needs no
 * cooperation from the port partner (unlike a PR_Swap, which the controller
 * refuses outright while dead-battery is set).
 *
 * Read-modify-write, preserving the other 15 bytes, and refusing outright when
 * the register reads back all zeros.
 */
static esp_err_t set_port_type(uint8_t port_type, const char *label)
{
    if (xSemaphoreTake(s_op_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;   /* another role change in flight -> 409 */
    }
    s_busy = true;

    esp_err_t err;
    uint8_t cfg[16];

    if (!i2cbus_lock(pdMS_TO_TICKS(2000))) {
        err = ESP_ERR_TIMEOUT;
        goto out;
    }
    err = reg_read(REG_PORT_CONFIG, cfg, sizeof(cfg));
    if (err != ESP_OK) {
        i2cbus_unlock();
        goto out;
    }

    bool all_zero = true;
    for (size_t i = 0; i < sizeof(cfg); i++) {
        if (cfg[i]) { all_zero = false; break; }
    }
    if (all_zero) {
        i2cbus_unlock();
        LOGE(TAG, "%s: PORT_CONFIG reads all zeros — IC has no configuration "
                  "loaded, refusing to write", label);
        err = ESP_ERR_INVALID_STATE;
        goto out;
    }

    if ((cfg[0] & PORT_TYPE_MASK) != port_type) {
        cfg[0] = (uint8_t)((cfg[0] & ~PORT_TYPE_MASK) | port_type);

        uint8_t clear[11];
        memset(clear, 0xFF, sizeof(clear));
        reg_write(REG_INT_CLEAR, clear, sizeof(clear));

        err = reg_write(REG_PORT_CONFIG, cfg, sizeof(cfg));
        i2cbus_unlock();
        if (err != ESP_OK) {
            goto out;
        }
        vTaskDelay(pdMS_TO_TICKS(PORT_SETTLE_MS));
    } else {
        i2cbus_unlock();
        LOGD(TAG, "%s: PORT_CONFIG already %u", label, port_type);
    }

    /* Confirm once. Nothing retries: a request either lands or is logged as
     * unconfirmed, because retrying a port disconnect/reconnect on a cable
     * that is permanently attached to the iPad is worse than reporting it. */
    bool src = false;
    int  vbus = 0;
    if (port_state(&src, &vbus) == ESP_OK) {
        bool want_src = (port_type == PORT_TYPE_SOURCE);
        if (src == want_src) {
            LOGI(TAG, "%s: done port_role=%s vbus=%d",
                 label, src ? "SRC" : "SNK", vbus);
        } else {
            LOGW(TAG, "%s: port role not confirmed — port_role=%s vbus=%d",
                 label, src ? "SRC" : "SNK", vbus);
        }
    }
    err = ESP_OK;

out:
    s_busy = false;
    xSemaphoreGive(s_op_lock);
    return err;
}

/*
 * Boot-time configuration only: Source power role + UFP data role. Does not
 * change the currently negotiated role. Same all-zero refusal as above — the
 * shipped device logged "power role is 0x00, setting Source" on every boot,
 * which was it writing a blank Application Customization block back each time.
 */
static void ensure_source_ufp(void)
{
    uint8_t cfg[16];

    if (!i2cbus_lock(pdMS_TO_TICKS(2000))) {
        LOGW(TAG, "ensure_source_ufp: I2C busy");
        return;
    }
    if (reg_read(REG_PORT_CONFIG, cfg, sizeof(cfg)) != ESP_OK) {
        i2cbus_unlock();
        LOGW(TAG, "ensure_source_ufp: PORT_CONFIG read failed");
        return;
    }

    bool all_zero = true;
    for (size_t i = 0; i < sizeof(cfg); i++) {
        if (cfg[i]) { all_zero = false; break; }
    }
    if (all_zero) {
        i2cbus_unlock();
        LOGE(TAG, "ensure_source_ufp: PORT_CONFIG reads all zeros — "
                  "IC unconfigured, refusing to write");
        return;
    }

    bool changed = false;
    if ((cfg[0] & PORT_TYPE_MASK) != PORT_TYPE_SOURCE) {
        LOGW(TAG, "ensure_source_ufp: port type is 0x%02X, setting Source-only",
             cfg[0] & PORT_TYPE_MASK);
        cfg[0] = (uint8_t)((cfg[0] & ~PORT_TYPE_MASK) | PORT_TYPE_SOURCE);
        changed = true;
    }
    if (cfg[0] & BIT_INIT_DATA_DFP) {
        LOGW(TAG, "ensure_source_ufp: initial data role is DFP, setting UFP");
        cfg[0] &= (uint8_t)~BIT_INIT_DATA_DFP;
        changed = true;
    }

    if (changed) {
        reg_write(REG_PORT_CONFIG, cfg, sizeof(cfg));
        i2cbus_unlock();
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        i2cbus_unlock();
        LOGI(TAG, "ensure_source_ufp: power=Source data=UFP OK");
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

bool tps_is_ready(void) { return s_ready; }
bool tps_is_busy(void)  { return s_busy; }

esp_err_t tps_read_status(tps_status_t *out)
{
    memset(out, 0, sizeof(*out));

    uint8_t mode[4], tcs[5], pdo[6], st[5], boot[5], pdst[4];

    if (!i2cbus_lock(pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = reg_read(REG_MODE, mode, sizeof(mode));
    if (err == ESP_OK) err = reg_read(REG_TYPE_C_STATUS, tcs, sizeof(tcs));
    if (err == ESP_OK) err = reg_read(REG_ACTIVE_PDO, pdo, sizeof(pdo));
    if (err == ESP_OK) err = reg_read(REG_STATUS, st, sizeof(st));
    if (err == ESP_OK) err = reg_read(REG_BOOT_FLAGS, boot, sizeof(boot));
    esp_err_t pd_err = reg_read(REG_PD_STATUS, pdst, sizeof(pdst));
    i2cbus_unlock();

    if (err != ESP_OK) {
        return err;
    }

    /* Mode is 4 ASCII bytes, space-padded: "APP " -> "APP". */
    size_t n = 0;
    for (size_t i = 0; i < 4; i++) {
        if (mode[i] > 0x20 && mode[i] < 0x7F) out->mode[n++] = (char)mode[i];
    }
    out->mode[n] = '\0';

    out->connected    = (tcs[0] & BIT_CC_CONNECTED) != 0;
    out->plug_present = (st[0]  & BIT_PLUG_PRESENT) != 0;
    out->contract     = (pdo[0] | pdo[1] | pdo[2] | pdo[3]) != 0;

    decode_pdo(pdo, &out->voltage_mv, &out->current_ma);
    out->power_profile_w = (out->voltage_mv && out->current_ma)
                               ? (out->voltage_mv * out->current_ma) / 1000000
                               : (out->contract ? 20 : 15);

    bool src = (st[0] & BIT_PORT_ROLE_SRC) != 0;
    strlcpy(out->port_role, src ? "source" : "sink", sizeof(out->port_role));

    if (pd_err == ESP_OK) {
        strlcpy(out->power_role,
                (pdst[0] & BIT_PD_POWER_SRC) ? "source" : "sink",
                sizeof(out->power_role));
        out->fault = false;
    } else {
        strlcpy(out->power_role, "unknown", sizeof(out->power_role));
        out->fault = true;
    }

    strlcpy(out->data_role, (st[0] & BIT_DATA_ROLE_DFP) ? "DFP" : "UFP",
            sizeof(out->data_role));

    out->vbus     = (st[2] >> 4) & 0x03;
    out->sourcing = src && (out->vbus == VBUS_5V || out->vbus == VBUS_IN_RANGE);

    out->dead_battery = (boot[0] & BOOT_DEAD_BATTERY) != 0;
    out->revision     = boot[4];

    char scratch[8];
    strlcpy(out->cfg_src, cfg_src_name((boot[3] >> 5) & 0x07, scratch, sizeof(scratch)),
            sizeof(out->cfg_src));

    strlcpy(s_mode_cache, out->mode, sizeof(s_mode_cache));
    return ESP_OK;
}

esp_err_t tps_enable_charging(void)
{
    /* Intent first: this write can fail, and what has to survive a power cut
     * is what the operator asked for, not whether the IC accepted it. */
    storage_set_i32(NVS_NS_PD, "charging", 1);
    return set_port_type(PORT_TYPE_SOURCE, "enable_charging");
}

esp_err_t tps_disable_charging(void)
{
    storage_set_i32(NVS_NS_PD, "charging", 0);
    /* Sink-only, not Disabled: the CC terminations stay alive so the port
     * keeps reporting plug state while charging is off. */
    return set_port_type(PORT_TYPE_SINK, "disable_charging");
}

const char *tps_charge_state(void)
{
    if (!s_ready || s_busy) {
        return "?";
    }
    bool src = false;
    int  vbus = 0;
    if (port_state(&src, &vbus) != ESP_OK) {
        return "?";
    }
    strlcpy(s_charge_cache,
            (src && (vbus == VBUS_5V || vbus == VBUS_IN_RANGE)) ? "ON" : "OFF",
            sizeof(s_charge_cache));
    return s_charge_cache;
}

const char *tps_mode_str(void)
{
    if (s_busy) {
        return "?";
    }
    uint8_t mode[4];
    if (reg_read_locked(REG_MODE, mode, sizeof(mode)) != ESP_OK) {
        return "?";
    }
    size_t n = 0;
    for (size_t i = 0; i < 4; i++) {
        if (mode[i] > 0x20 && mode[i] < 0x7F) s_mode_cache[n++] = (char)mode[i];
    }
    s_mode_cache[n] = '\0';
    return n ? s_mode_cache : "?";
}

/*
 * The IC loads its configuration from the TPS26750 EEPROM during its own
 * power-up. Until that finishes it sits in patch mode with no Type-C state
 * machine, no PD policy engine, and no way to source VBUS — and no host
 * command moves it. Restarting everything so the load runs again is the only
 * lever left.
 *
 * Bounded deliberately: an unprogrammed or miswired EEPROM never recovers, and
 * an unbounded retry would turn that into a permanent reboot loop on a device
 * that also drives a door. Past the limit the boot continues with charging
 * dead and everything else reachable.
 */
static void reset_for_app_mode(const char *mode, const char *where)
{
    if (!supervisor_take_reboot_quota(NVS_NS_PD, "appfail",
                                      CFG_PD_APP_MODE_MAX_RESETS)) {
        LOGE(TAG, "%s: mode=%s after %d reset(s) — giving up, PD stays disabled",
             where, mode, CFG_PD_APP_MODE_MAX_RESETS);
        return;
    }
    char detail[64];
    snprintf(detail, sizeof(detail), "%s mode=%s", where, mode);
    LOGE(TAG, "%s: mode=%s (not APP) — resetting ESP32", where, mode);
    supervisor_reboot_now(REBOOT_REASON_PD_NOT_APP, detail);
}

void tps_init(void)
{
    s_op_lock = xSemaphoreCreateMutexStatic(&s_op_lock_buf);
    s_ready   = false;

    LOGI(TAG, "init: addr=0x%02X", BOARD_TPS25751_ADDR);

    /* A power cycle is the operator saying "try again from scratch". */
    if (supervisor_is_power_on_boot()) {
        supervisor_clear_reboot_quota(NVS_NS_PD, "appfail");
    }

    /* Gate 1: APP mode. Typical boot is under 500 ms; allow 3 s on top of the
     * initial settle. Total worst case here is ~3.5 s, which is the budget the
     * spec gives PD init before network bring-up must proceed regardless. */
    vTaskDelay(pdMS_TO_TICKS(500));

    uint8_t mode[4] = { '?', '?', '?', '?' };
    bool in_app = false;
    for (int i = 0; i < 30; i++) {
        if (reg_read_locked(REG_MODE, mode, sizeof(mode)) == ESP_OK &&
            memcmp(mode, "APP ", 4) == 0) {
            in_app = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    char mode_str[8] = {0};
    size_t n = 0;
    for (size_t i = 0; i < 4; i++) {
        if (mode[i] > 0x20 && mode[i] < 0x7F) mode_str[n++] = (char)mode[i];
    }
    mode_str[n] = '\0';

    uint8_t boot[5] = {0};
    char summary[160];
    if (reg_read_retry(REG_BOOT_FLAGS, boot, sizeof(boot)) == ESP_OK) {
        boot_flags_summary(boot, summary, sizeof(summary));
    } else {
        strlcpy(summary, "unreadable", sizeof(summary));
    }
    LOGI(TAG, "init: mode=%s boot_flags %s", mode_str, summary);

    if (!in_app) {
        reset_for_app_mode(mode_str, "init");   /* may not return */
        return;
    }

    /* Gate 2: dead battery. TRM 4.4.1 — while the flag is set the port "will
     * only act as a Type-C sink regardless of the configuration" and every
     * PR_Swap to Source is refused, so charging is impossible until it clears.
     * Clearing it also moves the controller's own supply from VBUS back to the
     * 3.3 V rail, which is why it is switchable: that rail must be present. */
    if (boot[0] & BOOT_DEAD_BATTERY) {
#if CFG_PD_CLEAR_DEAD_BATTERY
        LOGW(TAG, "init: dead-battery flag set — sending DBfg "
                  "(moves IC supply to the 3V3 rail)");
        /* No output data for this task, so no return code to check. */
        if (send_4cc(CMD_DBFG, NULL, 0, false) == ESP_OK) {
            if (reg_read_retry(REG_BOOT_FLAGS, boot, sizeof(boot)) == ESP_OK) {
                boot_flags_summary(boot, summary, sizeof(summary));
                LOGI(TAG, "init: after DBfg: %s", summary);
            }
        } else {
            LOGE(TAG, "init: DBfg failed — charging will stay blocked");
        }
#else
        LOGE(TAG, "init: dead-battery flag set and clear_dead_battery is off "
                  "— charging is blocked");
#endif
    }

    /* Gate 3: port configuration. */
    ensure_source_ufp();

    if (i2cbus_lock(pdMS_TO_TICKS(1000))) {
        uint8_t clear[11];
        memset(clear, 0xFF, sizeof(clear));
        reg_write(REG_INT_CLEAR, clear, sizeof(clear));

        uint8_t mask[11] = {0};
        uint64_t m = INT_MASK_ALL;
        for (int i = 0; i < 8; i++) {
            mask[i] = (uint8_t)((m >> (8 * i)) & 0xFF);
        }
        reg_write(REG_INT_MASK, mask, sizeof(mask));
        i2cbus_unlock();
    }

    /*
     * Restore the charging state from before the power cut. ensure_source_ufp()
     * has just put the port in Source-only, so a remembered ON needs no further
     * command; a remembered OFF costs one PORT_CONFIG write. Charging is on for
     * the few seconds init() takes to get here — that window can only be closed
     * in the TPS26750's own configuration, not from this firmware.
     *
     * Empty NVS defaults to ON, matching what ensure_source_ufp() leaves.
     * A failed restore must not propagate: in the Python build it did, leaving
     * the driver permanently "not ready" and the API answering 503 for the rest
     * of the run even though the IC was fine.
     */
    int32_t want_on = storage_get_i32(NVS_NS_PD, "charging", 1);
    if (want_on) {
        LOGI(TAG, "init: charging ON restored (port already Source — no write)");
    } else {
        LOGI(TAG, "init: charging OFF restored — port to Sink-only");
        if (set_port_type(PORT_TYPE_SINK, "restore_off") != ESP_OK) {
            LOGW(TAG, "init: restoring charging OFF failed — continuing");
        }
    }

    /*
     * Re-read the mode on purpose. A board has been observed passing the wait
     * loop above and reporting PTCH by the end of init, which makes every
     * value read in between meaningless.
     */
    tps_status_t st;
    if (tps_read_status(&st) != ESP_OK) {
        LOGE(TAG, "init: post-config status read failed — PD stays disabled");
        return;
    }
    LOGI(TAG, "init: mode=%s rev=0x%02X cfg_src=%s port=%s vbus=%d sourcing=%d "
              "profile=%dW",
         st.mode, st.revision, st.cfg_src, st.port_role, st.vbus,
         st.sourcing ? 1 : 0, st.power_profile_w);

    if (strcmp(st.mode, "APP") != 0) {
        reset_for_app_mode(st.mode, "init (post-config)");
        return;
    }

    supervisor_clear_reboot_quota(NVS_NS_PD, "appfail");
    s_ready = true;
}

/* ── Periodic dump ───────────────────────────────────────────────────────── */

static void dump_state(void)
{
    uint8_t mode[4], tcs[5], pdo[6], cmd[4], cfg[16], st[5], pdst[4], boot[5];

    if (!i2cbus_lock(pdMS_TO_TICKS(1000))) return;
    bool ok = reg_read(REG_MODE, mode, sizeof(mode)) == ESP_OK &&
              reg_read(REG_TYPE_C_STATUS, tcs, sizeof(tcs)) == ESP_OK &&
              reg_read(REG_ACTIVE_PDO, pdo, sizeof(pdo)) == ESP_OK &&
              reg_read(REG_CMD_1, cmd, sizeof(cmd)) == ESP_OK &&
              reg_read(REG_PORT_CONFIG, cfg, sizeof(cfg)) == ESP_OK &&
              reg_read(REG_STATUS, st, sizeof(st)) == ESP_OK &&
              reg_read(REG_PD_STATUS, pdst, sizeof(pdst)) == ESP_OK &&
              reg_read(REG_BOOT_FLAGS, boot, sizeof(boot)) == ESP_OK;
    i2cbus_unlock();
    if (!ok) return;

    char summary[160];
    boot_flags_summary(boot, summary, sizeof(summary));

    int  vbus = (st[2] >> 4) & 0x03;
    bool src  = (st[0] & BIT_PORT_ROLE_SRC) != 0;
    uint8_t role = cfg[0] & PORT_TYPE_MASK;

    LOGD(TAG, "--- TPS periodic state ---");
    LOGD(TAG, "  mode=%c%c%c%c boot_flags %s",
         mode[0], mode[1], mode[2], mode[3], summary);
    LOGD(TAG, "  cable=%s advert=%s plug=%s",
         (tcs[0] & BIT_CC_CONNECTED) ? "connected" : "not connected",
         (tcs[0] & BIT_CURRENT_3A) ? "3A" :
             ((tcs[0] & BIT_CURRENT_1A5) ? "1.5A" : "default"),
         (st[0] & BIT_PLUG_PRESENT) ? "YES" : "no");
    LOGD(TAG, "  port_role=%s (STATUS bit5) vbus=%s",
         src ? "Source" : "Sink",
         vbus == VBUS_SAFE0V ? "vSafe0V" :
         vbus == VBUS_5V     ? "vSafe5V" :
         vbus == VBUS_IN_RANGE ? "in range" : "out of range");
    LOGD(TAG, "  pd_power_role=%s data_role=%s",
         (pdst[0] & BIT_PD_POWER_SRC) ? "Source" : "Sink",
         (st[0] & BIT_DATA_ROLE_DFP) ? "DFP" : "UFP");
    LOGD(TAG, "  port_cfg=%s contract=%s cmd1=%s",
         role == PORT_TYPE_SOURCE ? "SRC" :
         role == PORT_TYPE_SINK   ? "SNK" :
         role == PORT_TYPE_DRP    ? "DRP" : "DISABLED",
         (pdo[0] | pdo[1] | pdo[2] | pdo[3]) ? "active" : "none",
         (cmd[0] | cmd[1] | cmd[2] | cmd[3]) ? "busy" : "idle");
}

static void periodic_task(void *arg)
{
    (void)arg;
    LOGI(TAG, "periodic task started (%d s)", CFG_PD_PERIODIC_S);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CFG_PD_PERIODIC_S * 1000));
        supervisor_beat("pd", 30000);

        /* Skip the dump while the IC is mid-reset — it NAKs every read — and
         * skip the I2C reads entirely unless somebody will see the output. */
        if (!s_ready || s_busy || !logx_enabled(LOGX_DEBUG)) {
            continue;
        }
        dump_state();
    }
}

void tps_start_periodic_task(void)
{
    xTaskCreate(periodic_task, "pd_periodic", 4096, NULL, 4, NULL);
}
