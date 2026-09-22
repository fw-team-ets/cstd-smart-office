#include "eth.h"
#include "board_pins.h"
#include "board_config.h"
#include "logx.h"
#include "storage.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "mdns.h"

static const char *TAG = "net";

static esp_eth_handle_t   s_eth;
static esp_netif_t       *s_netif;
static esp_eth_netif_glue_handle_t s_glue;

static char s_ip[16]       = "0.0.0.0";
static char s_mac[18]      = "00:00:00:00:00:00";
static char s_hostname[32] = "ste-000000";
static bool s_phy_link;    /* last state reported by the event loop */

/* ── Hardware reset ──────────────────────────────────────────────────────── */

static void w5500_hard_reset(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_W5500_RST_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    /* W5500 datasheet: RSTn low for at least 500 us; 500 ms is what the
     * shipped firmware used and what the boards in the field are known to
     * come up with, so it stays. */
    gpio_set_level(BOARD_W5500_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(BOARD_W5500_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    LOGI(TAG, "W5500 reset done");
}

/* ── Static IP ───────────────────────────────────────────────────────────── */

/*
 * Applied only when the interface is built or hard-reset. The Python driver
 * re-applied it on every retry (~3 s), rewriting the address in the middle of
 * autonegotiation — which is exactly the tear-down that stopped the link ever
 * coming up on cables that needed more than one retry to negotiate.
 */
static void apply_static_ip(void)
{
    if (storage_get_i32(NVS_NS_NETCFG, "use_dhcp", CFG_NET_USE_DHCP_DEFAULT)) {
        return;
    }

    char ip[32], mask[32], gw[32], dns[32];
    if (!storage_get_str(NVS_NS_NETCFG, "ip", ip, sizeof(ip)))
        strlcpy(ip, CFG_NET_STATIC_IP_DEFAULT, sizeof(ip));
    if (!storage_get_str(NVS_NS_NETCFG, "subnet", mask, sizeof(mask)))
        strlcpy(mask, CFG_NET_STATIC_NETMASK_DEF, sizeof(mask));
    if (!storage_get_str(NVS_NS_NETCFG, "gateway", gw, sizeof(gw)))
        strlcpy(gw, CFG_NET_STATIC_GW_DEFAULT, sizeof(gw));
    if (!storage_get_str(NVS_NS_NETCFG, "dns", dns, sizeof(dns)))
        strlcpy(dns, CFG_NET_STATIC_DNS_DEFAULT, sizeof(dns));

    esp_netif_dhcpc_stop(s_netif);

    esp_netif_ip_info_t info = {0};
    info.ip.addr      = esp_ip4addr_aton(ip);
    info.netmask.addr = esp_ip4addr_aton(mask);
    info.gw.addr      = esp_ip4addr_aton(gw);
    esp_netif_set_ip_info(s_netif, &info);

    esp_netif_dns_info_t dns_info = {0};
    dns_info.ip.type      = ESP_IPADDR_TYPE_V4;
    dns_info.ip.u_addr.ip4.addr = esp_ip4addr_aton(dns);
    esp_netif_set_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns_info);

    LOGI(TAG, "ETH static IP: %s/%s gw=%s dns=%s", ip, mask, gw, dns);
}

/* ── Events ──────────────────────────────────────────────────────────────── */

static void eth_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;

    switch (id) {
    case ETHERNET_EVENT_CONNECTED: {
        s_phy_link = true;
        uint8_t mac[6] = {0};
        esp_eth_ioctl(s_eth, ETH_CMD_G_MAC_ADDR, mac);
        snprintf(s_mac, sizeof(s_mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        LOGI(TAG, "ETH PHY link up: MAC=%s", s_mac);
        break;
    }
    case ETHERNET_EVENT_DISCONNECTED:
        s_phy_link = false;
        LOGW(TAG, "ETH PHY link down");
        break;
    case ETHERNET_EVENT_START:
        LOGI(TAG, "ETH started");
        break;
    case ETHERNET_EVENT_STOP:
        s_phy_link = false;
        LOGI(TAG, "ETH stopped");
        break;
    default:
        break;
    }
}

static void got_ip_handler(void *arg, esp_event_base_t base,
                           int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    const esp_netif_ip_info_t *ip = &event->ip_info;

    char ipbuf[16], mask[16], gw[16];
    snprintf(ipbuf, sizeof(ipbuf), IPSTR, IP2STR(&ip->ip));
    snprintf(mask,  sizeof(mask),  IPSTR, IP2STR(&ip->netmask));
    snprintf(gw,    sizeof(gw),    IPSTR, IP2STR(&ip->gw));

    esp_netif_dns_info_t dns = {0};
    esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns);
    char dnsbuf[16];
    snprintf(dnsbuf, sizeof(dnsbuf), IPSTR, IP2STR(&dns.ip.u_addr.ip4));

    if (strcmp(s_ip, ipbuf) != 0) {
        LOGI(TAG, "IP changed: %s -> %s", s_ip, ipbuf);
    }
    strlcpy(s_ip, ipbuf, sizeof(s_ip));

    /* The exact wording of this line is what field diagnostics greps for. */
    LOGI(TAG, "ETH link up: MAC=%s IP=%s subnet=%s gw=%s dns=%s",
         s_mac, ipbuf, mask, gw, dnsbuf);
}

/* ── mDNS ────────────────────────────────────────────────────────────────── */

static void start_mdns(void)
{
    /*
     * Hostname is ste-<last 6 MAC hex>.local. Safari cannot be pointed at a
     * self-signed cert by IP, and a DHCP address can move, so the TLS cert's
     * SAN is this name — see tools/gen_certs.sh.
     */
    uint8_t mac[6] = {0};
    if (esp_eth_ioctl(s_eth, ETH_CMD_G_MAC_ADDR, mac) == ESP_OK) {
        snprintf(s_hostname, sizeof(s_hostname), CFG_NET_MDNS_PREFIX "%02x%02x%02x",
                 mac[3], mac[4], mac[5]);
    }

    if (mdns_init() != ESP_OK) {
        LOGW(TAG, "mDNS init failed — the device is reachable by IP only");
        return;
    }
    mdns_hostname_set(s_hostname);
    mdns_instance_name_set("ETeams smart room controller");
    mdns_service_add(NULL, "_https", "_tcp", CFG_SERVER_PORT, NULL, 0);
    LOGI(TAG, "mDNS: %s.local", s_hostname);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t eth_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    w5500_hard_reset();

    spi_bus_config_t buscfg = {
        .mosi_io_num     = BOARD_W5500_MOSI_GPIO,
        .miso_io_num     = BOARD_W5500_MISO_GPIO,
        .sclk_io_num     = BOARD_W5500_SCLK_GPIO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_W5500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .command_bits     = 16,   /* W5500 address phase */
        .address_bits     = 8,    /* W5500 control phase */
        .mode             = 0,
        .clock_speed_hz   = BOARD_W5500_SPI_HZ,
        .spics_io_num     = BOARD_W5500_CS_GPIO,
        .queue_size       = 20,
    };

    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(BOARD_W5500_SPI_HOST, &devcfg);
    w5500_cfg.int_gpio_num = BOARD_W5500_INT_GPIO;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr     = 1;
    phy_cfg.reset_gpio_num = -1;   /* already reset above, by hand */

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    if (!mac || !phy) {
        LOGE(TAG, "W5500 MAC/PHY allocation failed");
        return ESP_FAIL;
    }

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &s_eth));

    /*
     * The W5500 has no factory MAC of its own, so one derived from the ESP32's
     * base MAC is written into it. Doing this explicitly keeps the address
     * stable across reboots, which matters because the customer's DHCP server
     * hands out a reservation by MAC.
     */
    uint8_t mac_addr[6];
    ESP_ERROR_CHECK(esp_read_mac(mac_addr, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth, ETH_CMD_S_MAC_ADDR, mac_addr));
    snprintf(s_mac, sizeof(s_mac), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_netif = esp_netif_new(&netif_cfg);
    s_glue  = esp_eth_new_netif_glue(s_eth);
    ESP_ERROR_CHECK(esp_netif_attach(s_netif, s_glue));

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               got_ip_handler, NULL));

    apply_static_ip();

    LOGI(TAG, "ETH pins: mosi=%d miso=%d sck=%d cs=%d rst=%d int=%d @%dHz",
         BOARD_W5500_MOSI_GPIO, BOARD_W5500_MISO_GPIO, BOARD_W5500_SCLK_GPIO,
         BOARD_W5500_CS_GPIO, BOARD_W5500_RST_GPIO, BOARD_W5500_INT_GPIO,
         BOARD_W5500_SPI_HZ);

    /* Non-blocking: this returns as soon as the driver task is running, and
     * the link/IP arrive later through the event loop. */
    ESP_ERROR_CHECK(esp_eth_start(s_eth));

    start_mdns();
    return ESP_OK;
}

bool eth_link_up(void)
{
    if (!s_netif) return false;
    if (!s_phy_link) return false;

    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_netif, &info) != ESP_OK) {
        /* An interface we cannot interrogate is not one we can route through. */
        return false;
    }
    return info.ip.addr != 0;
}

bool eth_refresh(void)
{
    char ipbuf[16] = "0.0.0.0";
    bool up = false;

    if (s_netif && s_phy_link) {
        esp_netif_ip_info_t info;
        if (esp_netif_get_ip_info(s_netif, &info) == ESP_OK && info.ip.addr != 0) {
            snprintf(ipbuf, sizeof(ipbuf), IPSTR, IP2STR(&info.ip));
            up = true;
        }
    }

    if (strcmp(ipbuf, s_ip) != 0) {
        /* The Python build wrote its cached IP once at boot and never again,
         * so /status and the LCD reported the boot-time address forever,
         * including long after the cable was pulled. */
        LOGI(TAG, "IP changed: %s -> %s", s_ip, ipbuf);
        strlcpy(s_ip, ipbuf, sizeof(s_ip));
    }
    return up;
}

const char *eth_get_ip(void)       { return s_ip; }
const char *eth_get_mac(void)      { return s_mac; }
const char *eth_get_hostname(void) { return s_hostname; }

void eth_reset_phy(void)
{
    LOGW(TAG, "resetting W5500");
    if (s_eth) {
        esp_eth_stop(s_eth);
    }
    w5500_hard_reset();
    if (s_eth) {
        esp_eth_start(s_eth);
    }
    apply_static_ip();
}
