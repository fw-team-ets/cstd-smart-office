#include "api.h"
#include "board_config.h"
#include "logx.h"
#include "storage.h"
#include "pairing.h"
#include "relay.h"
#include "display.h"
#include "tps25751.h"
#include "eth.h"
#include "netmon.h"
#include "supervisor.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_server.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "cJSON.h"

/* For the peer-address lookup in client_ip(). */
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "api";

static httpd_handle_t s_server;

/* Embedded at build time by main/CMakeLists.txt. No VFS partition exists in
 * this firmware, so the cert and key travel inside the single .bin. */
extern const uint8_t servercert_start[] asm("_binary_tls_server_cert_pem_start");
extern const uint8_t servercert_end[]   asm("_binary_tls_server_cert_pem_end");
extern const uint8_t serverkey_start[]  asm("_binary_tls_server_key_pem_start");
extern const uint8_t serverkey_end[]    asm("_binary_tls_server_key_pem_end");

#define MAX_BODY 1024

/* ── Response helpers ────────────────────────────────────────────────────── */

/*
 * The webapp is served from a different origin, so every response carries CORS
 * headers and OPTIONS is answered. Without this Safari does not even send the
 * real request and the failure looks like the device being offline.
 */
static void add_cors(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers",
                       "Content-Type, Authorization");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");
}

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *root)
{
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        add_cors(req);
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"oom\"}");
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    add_cors(req);
    esp_err_t err = httpd_resp_sendstr(req, body);
    free(body);
    return err;
}

static esp_err_t send_ok(httpd_req_t *req, cJSON *data)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddItemToObject(root, "data", data ? data : cJSON_CreateObject());
    return send_json(req, "200 OK", root);
}

static esp_err_t send_err(httpd_req_t *req, const char *status, const char *error)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", error);
    return send_json(req, status, root);
}

/* ── Auth ────────────────────────────────────────────────────────────────── */

static const char *client_ip(httpd_req_t *req, char *buf, size_t len)
{
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) return NULL;

    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) != 0) {
        return NULL;
    }
    if (addr.ss_family != AF_INET) {
        return NULL;
    }
    struct sockaddr_in *in = (struct sockaddr_in *)&addr;
    inet_ntop(AF_INET, &in->sin_addr, buf, len);
    return buf;
}

/*
 * Bearer gate. On success it also records that the iPad is alive and tracks a
 * changed client address, which is what lets the ping watchdog stay quiet
 * while the iPad is actually talking to us.
 */
static bool require_token(httpd_req_t *req)
{
    char hdr[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
        return false;
    }
    if (strncmp(hdr, "Bearer ", 7) != 0) {
        return false;
    }
    if (!pairing_check_token(hdr + 7)) {
        return false;
    }

    netmon_note_peer_seen();
    char ip[46];
    if (client_ip(req, ip, sizeof(ip))) {
        pairing_set_peer_ip(ip);
    }
    return true;
}

/* ── Body reading ────────────────────────────────────────────────────────── */

static cJSON *read_body(httpd_req_t *req)
{
    if (req->content_len == 0) {
        return cJSON_CreateObject();   /* an empty body is a valid {} */
    }
    if (req->content_len >= MAX_BODY) {
        return NULL;
    }
    char buf[MAX_BODY];
    size_t received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) {
            /* A short read is a client that went away mid-body; there is
             * nothing to parse and nothing to wait for. */
            return NULL;
        }
        received += (size_t)r;
    }
    buf[received] = '\0';
    return cJSON_Parse(buf);
}

/* ── Handlers ────────────────────────────────────────────────────────────── */

static esp_err_t h_options(httpd_req_t *req)
{
    add_cors(req);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/*
 * Public on purpose: this is what QA and the network admin call to confirm
 * which build is on a device before running the release test list. Requiring a
 * token would mean a device that has never been paired could not be identified
 * at all. It exposes nothing a port scan would not already reveal.
 */
static esp_err_t h_info(httpd_req_t *req)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "company",  FW_COMPANY);
    cJSON_AddStringToObject(d, "version",  FW_VERSION);
    cJSON_AddStringToObject(d, "hardware", FW_HARDWARE);
    cJSON_AddStringToObject(d, "bin",      FW_BIN_NAME);
    cJSON_AddStringToObject(d, "mac",      eth_get_mac());
    cJSON_AddStringToObject(d, "ip",       eth_get_ip());
    cJSON_AddStringToObject(d, "hostname", eth_get_hostname());
    cJSON_AddBoolToObject(  d, "paired",   pairing_is_paired());
    return send_ok(req, d);
}

static esp_err_t h_pair_start(httpd_req_t *req)
{
    if (pairing_is_paired()) {
        return send_err(req, "409 Conflict", "already_paired");
    }
    int expires_in = 0;
    if (!pairing_start(&expires_in)) {
        return send_err(req, "409 Conflict", "already_paired");
    }
    cJSON *d = cJSON_CreateObject();
    cJSON_AddNumberToObject(d, "expires_in", expires_in);
    return send_ok(req, d);
}

static esp_err_t h_pair_confirm(httpd_req_t *req)
{
    if (pairing_is_paired()) {
        return send_err(req, "409 Conflict", "already_paired");
    }

    cJSON *body = read_body(req);
    if (!body) {
        return send_err(req, "400 Bad Request", "invalid_json");
    }
    cJSON *otp = cJSON_GetObjectItem(body, "otp");
    if (!cJSON_IsString(otp)) {
        cJSON_Delete(body);
        return send_err(req, "400 Bad Request", "otp_required");
    }

    char token[80];
    const char *err = NULL;
    bool ok = pairing_confirm(otp->valuestring, token, sizeof(token), &err);
    cJSON_Delete(body);

    if (!ok) {
        /* "not in pairing mode" is a state problem, a wrong code is a data
         * problem — the webapp shows different things for the two. */
        const char *status = (strcmp(err, "not_in_pairing_mode") == 0)
                                 ? "409 Conflict" : "400 Bad Request";
        return send_err(req, status, err);
    }

    /* Lock the door on a successful pairing, matching the Python build: an
     * unpaired device is left open, a paired one is under control. */
    relay_set_maglock(MAGLOCK_LOCKED);

    char ip[46];
    if (client_ip(req, ip, sizeof(ip))) {
        pairing_set_peer_ip(ip);
    }
    netmon_note_peer_seen();
    supervisor_clear_reboot_quota(NVS_NS_WDT, "peerfail");

    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "token", token);   /* the only time it is sent */
    return send_ok(req, d);
}

static esp_err_t h_unpair(httpd_req_t *req)
{
    if (!require_token(req)) {
        return send_err(req, "401 Unauthorized", "invalid_token");
    }
    pairing_unpair();
    /* The door is left as it is. Unpairing over the network is an
     * administrative act, not a reason to change the physical state of a door
     * that somebody may be standing at. */
    return send_ok(req, NULL);
}

static void add_pd_status(cJSON *parent)
{
    if (!tps_is_ready()) {
        cJSON_AddBoolToObject(parent, "pd_ready", false);
        cJSON_AddNullToObject(parent, "pd");
        return;
    }
    if (tps_is_busy()) {
        /* The port disconnects and reconnects for ~1.5 s around a role change;
         * reporting the transient as a fault would be wrong. */
        cJSON_AddBoolToObject(parent, "pd_ready", true);
        cJSON *pd = cJSON_CreateObject();
        cJSON_AddBoolToObject(pd, "busy", true);
        cJSON_AddItemToObject(parent, "pd", pd);
        return;
    }

    tps_status_t st;
    if (tps_read_status(&st) != ESP_OK) {
        cJSON_AddBoolToObject(parent, "pd_ready", true);
        cJSON *pd = cJSON_CreateObject();
        cJSON_AddStringToObject(pd, "error", "read_failed");
        cJSON_AddItemToObject(parent, "pd", pd);
        return;
    }

    cJSON_AddBoolToObject(parent, "pd_ready", true);
    cJSON_AddBoolToObject(parent, "charging", st.sourcing);
    cJSON_AddNumberToObject(parent, "vbus", st.vbus);

    cJSON *pd = cJSON_CreateObject();
    cJSON_AddStringToObject(pd, "mode",            st.mode);
    cJSON_AddBoolToObject(  pd, "connected",       st.connected);
    cJSON_AddBoolToObject(  pd, "plug_present",    st.plug_present);
    cJSON_AddBoolToObject(  pd, "contract",        st.contract);
    cJSON_AddNumberToObject(pd, "power_profile_w", st.power_profile_w);
    cJSON_AddNumberToObject(pd, "voltage_mv",      st.voltage_mv);
    cJSON_AddNumberToObject(pd, "current_ma",      st.current_ma);
    cJSON_AddStringToObject(pd, "port_role",       st.port_role);
    cJSON_AddStringToObject(pd, "power_role",      st.power_role);
    cJSON_AddStringToObject(pd, "data_role",       st.data_role);
    cJSON_AddNumberToObject(pd, "vbus",            st.vbus);
    cJSON_AddBoolToObject(  pd, "sourcing",        st.sourcing);
    cJSON_AddBoolToObject(  pd, "dead_battery",    st.dead_battery);
    cJSON_AddStringToObject(pd, "cfg_src",         st.cfg_src);
    cJSON_AddBoolToObject(  pd, "fault",           st.fault);
    cJSON_AddNumberToObject(pd, "revision",        st.revision);
    cJSON_AddItemToObject(parent, "pd", pd);
}

static esp_err_t h_status(httpd_req_t *req)
{
    if (!require_token(req)) {
        return send_err(req, "401 Unauthorized", "invalid_token");
    }

    cJSON *d = cJSON_CreateObject();
    cJSON_AddBoolToObject(  d, "link",         eth_link_up());
    cJSON_AddStringToObject(d, "ip",           eth_get_ip());
    cJSON_AddStringToObject(d, "mac",          eth_get_mac());
    cJSON_AddStringToObject(d, "hostname",     eth_get_hostname());
    cJSON_AddBoolToObject(  d, "paired",       pairing_is_paired());
    cJSON_AddBoolToObject(  d, "ipad_online",  netmon_peer_online());
    cJSON_AddStringToObject(d, "ping",         netmon_ping_status());
    cJSON_AddNumberToObject(d, "uptime_s",     (double)(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(d, "reset_reason", supervisor_boot_cause());
    cJSON_AddStringToObject(d, "version",      FW_VERSION);

    cJSON *relays = cJSON_CreateObject();
    cJSON_AddNumberToObject(relays, "maglock", relay_get_maglock());
    cJSON_AddBoolToObject(relays, "failsafe", relay_failsafe_active());
    cJSON_AddItemToObject(d, "relays", relays);

    cJSON_AddNumberToObject(d, "heap_free", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(d, "heap_largest_block",
        (double)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    add_pd_status(d);
    return send_ok(req, d);
}

static esp_err_t h_charge(httpd_req_t *req)
{
    if (!require_token(req)) {
        return send_err(req, "401 Unauthorized", "invalid_token");
    }
    if (!tps_is_ready()) {
        return send_err(req, "503 Service Unavailable", "pd_not_ready");
    }
    if (tps_is_busy()) {
        return send_err(req, "409 Conflict", "pd_command_in_progress");
    }

    cJSON *body = read_body(req);
    if (!body) {
        return send_err(req, "400 Bad Request", "invalid_json");
    }
    cJSON *on = cJSON_GetObjectItem(body, "on");
    if (!cJSON_IsBool(on)) {
        cJSON_Delete(body);
        return send_err(req, "400 Bad Request", "on_required");
    }
    bool want_on = cJSON_IsTrue(on);
    cJSON_Delete(body);

    /*
     * The 80% / 20% battery policy lives in the webapp (spec v0.1 section
     * 4.3): the firmware does not compute anything, it only does what it is
     * told and remembers the last intent across a power cut.
     */
    esp_err_t err = want_on ? tps_enable_charging() : tps_disable_charging();
    if (err == ESP_ERR_INVALID_STATE) {
        return send_err(req, "409 Conflict", "pd_command_in_progress");
    }
    if (err != ESP_OK) {
        return send_err(req, "503 Service Unavailable", "pd_write_failed");
    }

    cJSON *d = cJSON_CreateObject();
    cJSON_AddBoolToObject(d, "charging", want_on);
    return send_ok(req, d);
}

static esp_err_t h_maglock(httpd_req_t *req)
{
    if (!require_token(req)) {
        return send_err(req, "401 Unauthorized", "invalid_token");
    }
    if (!relay_is_ready()) {
        return send_err(req, "503 Service Unavailable", "relay_not_ready");
    }

    cJSON *body = read_body(req);
    if (!body) {
        return send_err(req, "400 Bad Request", "invalid_json");
    }
    cJSON *v = cJSON_GetObjectItem(body, "value");
    if (!cJSON_IsNumber(v) || (v->valueint != 0 && v->valueint != 1)) {
        cJSON_Delete(body);
        return send_err(req, "400 Bad Request", "value_must_be_0_or_1");
    }
    int value = v->valueint;
    cJSON_Delete(body);

    if (relay_failsafe_active() && value == MAGLOCK_LOCKED) {
        /* The fail-safe exists because the controller could not be reached.
         * Honouring a lock command now would defeat it. */
        return send_err(req, "409 Conflict", "failsafe_active");
    }

    relay_set_maglock(value);

    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "relay", "maglock");
    cJSON_AddNumberToObject(d, "value", value);
    return send_ok(req, d);
}

static esp_err_t h_logs(httpd_req_t *req)
{
    if (!require_token(req)) {
        return send_err(req, "401 Unauthorized", "invalid_token");
    }

    /*
     * Capped, and the buffer is on the heap rather than this task's stack:
     * 40 x 160 bytes would not fit in an httpd worker stack.
     */
    size_t limit = 40;
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "limit", val, sizeof(val)) == ESP_OK) {
            int n = atoi(val);
            if (n > 0) limit = (n > 120) ? 120 : (size_t)n;
        }
    }

    char (*buf)[LOGX_LINE_MAX] = calloc(limit, LOGX_LINE_MAX);
    if (!buf) {
        return send_err(req, "503 Service Unavailable", "out_of_memory");
    }

    cJSON *d = cJSON_CreateObject();

    cJSON *logs = cJSON_CreateArray();
    size_t n = logx_get_recent(buf, limit);
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(logs, cJSON_CreateString(buf[i]));
    }
    cJSON_AddItemToObject(d, "logs", logs);

    cJSON *resets = cJSON_CreateArray();
    n = logx_get_resets(buf, (limit < CFG_RESET_RING_SIZE) ? limit : CFG_RESET_RING_SIZE);
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(resets, cJSON_CreateString(buf[i]));
    }
    cJSON_AddItemToObject(d, "resets", resets);

    cJSON *health = cJSON_CreateArray();
    n = logx_get_health(buf, (limit < CFG_HEALTH_RING_SIZE) ? limit : CFG_HEALTH_RING_SIZE);
    for (size_t i = 0; i < n; i++) {
        cJSON_AddItemToArray(health, cJSON_CreateString(buf[i]));
    }
    cJSON_AddItemToObject(d, "health", health);

    free(buf);
    return send_ok(req, d);
}

/* ── Registration ────────────────────────────────────────────────────────── */

static const httpd_uri_t s_routes[] = {
    { .uri = "/api/info",          .method = HTTP_GET,  .handler = h_info         },
    { .uri = "/api/pair/start",    .method = HTTP_POST, .handler = h_pair_start   },
    { .uri = "/api/pair/confirm",  .method = HTTP_POST, .handler = h_pair_confirm },
    { .uri = "/api/status",        .method = HTTP_GET,  .handler = h_status       },
    { .uri = "/api/charge",        .method = HTTP_POST, .handler = h_charge       },
    { .uri = "/api/relay/maglock", .method = HTTP_POST, .handler = h_maglock      },
    { .uri = "/api/logs",          .method = HTTP_GET,  .handler = h_logs         },
    { .uri = "/api/unpair",        .method = HTTP_POST, .handler = h_unpair       },

    /* CORS preflight for each route that the webapp POSTs to. */
    { .uri = "/api/pair/start",    .method = HTTP_OPTIONS, .handler = h_options },
    { .uri = "/api/pair/confirm",  .method = HTTP_OPTIONS, .handler = h_options },
    { .uri = "/api/charge",        .method = HTTP_OPTIONS, .handler = h_options },
    { .uri = "/api/relay/maglock", .method = HTTP_OPTIONS, .handler = h_options },
    { .uri = "/api/unpair",        .method = HTTP_OPTIONS, .handler = h_options },
    { .uri = "/api/status",        .method = HTTP_OPTIONS, .handler = h_options },
    { .uri = "/api/info",          .method = HTTP_OPTIONS, .handler = h_options },
    { .uri = "/api/logs",          .method = HTTP_OPTIONS, .handler = h_options },
};

esp_err_t api_start(void)
{
    httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();

    cfg.servercert     = servercert_start;
    cfg.servercert_len = servercert_end - servercert_start;
    cfg.prvtkey_pem    = serverkey_start;
    cfg.prvtkey_len    = serverkey_end - serverkey_start;

    cfg.port_secure    = CFG_SERVER_PORT;
    cfg.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;

    /*
     * INADDR_ANY, not the boot-time address. The Python build bound the IP it
     * happened to hold at boot; a DHCP renewal then left a listener bound to
     * an address the device no longer had — pingable, not connectable. That is
     * the field failure this rewrite exists to end, so it is worth saying out
     * loud: nothing here may ever pass a specific address.
     */
    cfg.httpd.ctrl_port       = 32768;
    cfg.httpd.max_open_sockets = CFG_SERVER_MAX_SOCKETS;
    cfg.httpd.max_uri_handlers =
        sizeof(s_routes) / sizeof(s_routes[0]) + 2;
    cfg.httpd.lru_purge_enable = true;   /* drop the oldest idle connection
                                          * rather than refusing a new one */
    cfg.httpd.stack_size      = 10240;   /* TLS handshake plus cJSON */
    cfg.httpd.recv_wait_timeout = 10;
    cfg.httpd.send_wait_timeout = 10;

    esp_err_t err = httpd_ssl_start(&s_server, &cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "httpd_ssl_start failed: %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < sizeof(s_routes) / sizeof(s_routes[0]); i++) {
        httpd_register_uri_handler(s_server, &s_routes[i]);
    }

    LOGI(TAG, "listening on https://0.0.0.0:%d", CFG_SERVER_PORT);
    LOGI(TAG, "free heap: %lu bytes largest block: %u",
         (unsigned long)esp_get_free_heap_size(),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return ESP_OK;
}
