/*
 * storage.h — typed NVS accessors.
 *
 * The namespace/key/type layout is inherited from the MicroPython build so an
 * already-provisioned unit keeps its settings. MicroPython's esp32.NVS stored
 * every string with set_blob(), NOT nvs_set_str(), so strings are read and
 * written here as blobs with no NUL terminator on flash.
 *
 *   auth   / paired    i32     1 = paired
 *   auth   / tok_hash  blob32  SHA-256 of the bearer token  (new in the C build)
 *   auth   / ipad_ip   blob    UTF-8 address of the paired iPad, <= 46
 *   netcfg / use_dhcp  i32     1 = DHCP, 0 = static; key absent = never set
 *   netcfg / ip|subnet|gateway|dns  blob  UTF-8, <= 32
 *   pd     / charging  i32     last requested charging INTENT
 *   pd     / appfail   i32     consecutive reboots because TPS never hit APP
 *   wdt    / peerfail  i32     consecutive reboots because the iPad went quiet
 *   wdt    / linkfail  i32     consecutive reboots because ETH link stayed down
 *   wdt    / bootfail  i32     consecutive reboots because no IP arrived
 *
 * The RSA/AES-GCM application layer is gone (spec v0.1 section 2), so
 * auth/aes_key is no longer written. storage_migrate_from_python() erases any
 * leftover copy rather than leaving a dead 32-byte session key in flash.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define NVS_NS_AUTH   "auth"
#define NVS_NS_NETCFG "netcfg"
#define NVS_NS_PD     "pd"
#define NVS_NS_WDT    "wdt"

esp_err_t storage_init(void);

/* Returns `def` when the key is missing or unreadable. */
int32_t storage_get_i32(const char *ns, const char *key, int32_t def);

/*
 * Reads the stored value first and writes only on a change. NVS lives in
 * flash and several of these keys are touched on every request or every tick;
 * the Python build learned this the same way (set_int_if_changed).
 */
esp_err_t storage_set_i32(const char *ns, const char *key, int32_t value);

/* Blob-backed string, NUL-terminated on the way out. Returns false if absent. */
bool storage_get_str(const char *ns, const char *key, char *out, size_t out_len);
esp_err_t storage_set_str(const char *ns, const char *key, const char *value);

bool storage_get_blob(const char *ns, const char *key, void *out, size_t out_len);
esp_err_t storage_set_blob(const char *ns, const char *key, const void *data, size_t len);

esp_err_t storage_erase_key(const char *ns, const char *key);

/* Drop keys the C firmware no longer uses (auth/aes_key from the RSA flow). */
void storage_migrate_from_python(void);
