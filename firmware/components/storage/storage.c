#include "storage.h"
#include "logx.h"

#include <string.h>
#include <limits.h>
#include <stdint.h>

#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "nvm";

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /*
         * Only reached on a blank or version-mismatched partition. Erasing
         * here loses the pairing, which is why it is not done silently.
         */
        LOGW(TAG, "NVS partition unusable (%s) — erasing and reinitialising",
             esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    }
    return err;
}

int32_t storage_get_i32(const char *ns, const char *key, int32_t def)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return def;
    }
    int32_t v = def;
    if (nvs_get_i32(h, key, &v) != ESP_OK) {
        v = def;
    }
    nvs_close(h);
    return v;
}

esp_err_t storage_set_i32(const char *ns, const char *key, int32_t value)
{
    /* Read-compare-write: see the header for why this is not optional. The
     * sentinel is picked so it can never equal `value`, without relying on
     * value-1 (which overflows at INT32_MIN). */
    int32_t sentinel = (value == INT32_MAX) ? INT32_MIN : value + 1;
    if (storage_get_i32(ns, key, sentinel) == value) {
        return ESP_OK;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        LOGW(TAG, "open %s failed: %s", ns, esp_err_to_name(err));
        return err;
    }
    err = nvs_set_i32(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        LOGD(TAG, "%s/%s = %ld", ns, key, (long)value);
    }
    return err;
}

bool storage_get_str(const char *ns, const char *key, char *out, size_t out_len)
{
    if (out_len == 0) return false;
    out[0] = '\0';

    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    /*
     * Read as a blob, not a string: MicroPython's esp32.NVS wrote these with
     * set_blob(), so there is no terminator on flash and nvs_get_str() would
     * reject them with ESP_ERR_NVS_TYPE_MISMATCH on a migrated unit.
     */
    size_t len = out_len - 1;
    esp_err_t err = nvs_get_blob(h, key, out, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return false;
    }
    out[len] = '\0';
    return len > 0;
}

esp_err_t storage_set_str(const char *ns, const char *key, const char *value)
{
    char current[64];
    if (storage_get_str(ns, key, current, sizeof(current)) &&
        strcmp(current, value) == 0) {
        return ESP_OK;
    }
    return storage_set_blob(ns, key, value, strlen(value));
}

bool storage_get_blob(const char *ns, const char *key, void *out, size_t out_len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = out_len;
    esp_err_t err = nvs_get_blob(h, key, out, &len);
    nvs_close(h);
    return err == ESP_OK && len == out_len;
}

esp_err_t storage_set_blob(const char *ns, const char *key,
                           const void *data, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t storage_erase_key(const char *ns, const char *key)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(h, key);
    if (err == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
    /* Absent is the desired end state, so "not found" is success. */
    return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
}

void storage_migrate_from_python(void)
{
    /*
     * The RSA/AES-GCM application layer is gone. A unit upgraded from the
     * MicroPython firmware still has its 32-byte AES session key sitting in
     * flash with nothing left to read it; erase it rather than leave key
     * material behind. The paired flag and iPad address stay, but a paired
     * unit with no bearer-token hash is treated as unpaired by pairing_load(),
     * so the iPad simply pairs again.
     */
    nvs_handle_t h;
    if (nvs_open(NVS_NS_AUTH, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    size_t len = 0;
    if (nvs_get_blob(h, "aes_key", NULL, &len) == ESP_OK) {
        LOGW("migrate", "erasing legacy auth/aes_key (%u bytes) — "
                        "the RSA/AES layer is not part of this firmware",
             (unsigned)len);
        nvs_erase_key(h, "aes_key");
        nvs_commit(h);
    }
    nvs_close(h);
}
