#include "i2cbus.h"
#include "board_pins.h"
#include "logx.h"

#include "driver/i2c_master.h"
#include "freertos/semphr.h"

static const char *TAG = "i2c";

static i2c_master_bus_handle_t s_bus;
static SemaphoreHandle_t       s_lock;
static StaticSemaphore_t       s_lock_buf;

/*
 * One device handle per address, created lazily. The new i2c_master driver is
 * handle-based; caching them avoids an add/remove pair around every transfer.
 */
#define MAX_DEVICES 4
static struct {
    uint8_t                     addr;
    i2c_master_dev_handle_t     handle;
} s_devs[MAX_DEVICES];
static size_t s_dev_count;

#define I2C_TIMEOUT_MS 200

static i2c_master_dev_handle_t dev_for(uint8_t addr)
{
    for (size_t i = 0; i < s_dev_count; i++) {
        if (s_devs[i].addr == addr) {
            return s_devs[i].handle;
        }
    }
    if (s_dev_count >= MAX_DEVICES) {
        return NULL;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = BOARD_I2C_FREQ_HZ,
    };
    i2c_master_dev_handle_t h;
    if (i2c_master_bus_add_device(s_bus, &cfg, &h) != ESP_OK) {
        return NULL;
    }
    s_devs[s_dev_count].addr   = addr;
    s_devs[s_dev_count].handle = h;
    s_dev_count++;
    return h;
}

esp_err_t i2cbus_init(void)
{
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);

    i2c_master_bus_config_t cfg = {
        .i2c_port                     = BOARD_I2C_PORT,
        .sda_io_num                   = BOARD_I2C_SDA_GPIO,
        .scl_io_num                   = BOARD_I2C_SCL_GPIO,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        LOGE(TAG, "init failed: %s", esp_err_to_name(err));
        return err;
    }
    LOGI(TAG, "init: SDA=GPIO%d SCL=GPIO%d %dHz (hardware I2C)",
         BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, BOARD_I2C_FREQ_HZ);
    return ESP_OK;
}

bool i2cbus_lock(TickType_t timeout_ticks)
{
    if (!s_lock) return false;
    return xSemaphoreTake(s_lock, timeout_ticks) == pdTRUE;
}

void i2cbus_unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

esp_err_t i2cbus_write(uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t h = dev_for(addr);
    if (!h) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(h, data, len, I2C_TIMEOUT_MS);
}

esp_err_t i2cbus_read(uint8_t addr, uint8_t *data, size_t len)
{
    i2c_master_dev_handle_t h = dev_for(addr);
    if (!h) return ESP_ERR_INVALID_STATE;
    return i2c_master_receive(h, data, len, I2C_TIMEOUT_MS);
}

esp_err_t i2cbus_write_read(uint8_t addr,
                            const uint8_t *wr, size_t wr_len,
                            uint8_t *rd, size_t rd_len)
{
    i2c_master_dev_handle_t h = dev_for(addr);
    if (!h) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(h, wr, wr_len, rd, rd_len, I2C_TIMEOUT_MS);
}

bool i2cbus_probe(uint8_t addr)
{
    if (!i2cbus_lock(pdMS_TO_TICKS(500))) {
        return false;
    }
    esp_err_t err = i2c_master_probe(s_bus, addr, I2C_TIMEOUT_MS);
    i2cbus_unlock();
    return err == ESP_OK;
}
