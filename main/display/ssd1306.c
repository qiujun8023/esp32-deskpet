#include "ssd1306.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#define I2C_SDA_PIN  8
#define I2C_SCL_PIN  9
#define I2C_SPEED_HZ 400000
#define SSD1306_ADDR 0x3C

static const char*             TAG = "ssd1306";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

/* SSD1306 上电初始化序列,数值按数据手册 §7.4 默认值裁剪,次序敏感勿重排 */
static const uint8_t s_init_cmds[] = {
    0xAE,
    0xD5, 0x80,
    0xA8, 0x3F,
    0xD3, 0x00,
    0x40,
    0x8D, 0x14,  // 电荷泵:模块无外部 7.5V 供电时必须开启
    0x20, 0x00,
    0xA1,        // 段重映射 + COM 反扫,和屏幕实际装配方向匹配
    0xC8,
    0xDA, 0x12,
    0x81, 0xCF,
    0xD9, 0xF1,
    0xDB, 0x40,
    0xA4,
    0xA6,
    0xAF,
};

static void write_cmd(uint8_t cmd) {
    // 控制字节 0x00:Co=0,D/C=0 表示随后单字节是命令
    uint8_t   buf[2] = {0x00, cmd};
    esp_err_t err    = i2c_master_transmit(s_dev, buf, 2, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c cmd 0x%02x failed: %s", cmd, esp_err_to_name(err));
    }
}

void ssd1306_init(void) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = I2C_SDA_PIN,
        .scl_io_num                   = I2C_SCL_PIN,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SSD1306_ADDR,
        .scl_speed_hz    = I2C_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));

    for (int i = 0; i < (int)sizeof(s_init_cmds); i++) {
        write_cmd(s_init_cmds[i]);
    }

    static uint8_t blank[SSD1306_BUFSIZE] = {0};
    ssd1306_flush(blank);

    ESP_LOGI(TAG, "ssd1306 ready");
}

void ssd1306_flush(const uint8_t* buf) {
    // 列地址 0~127
    write_cmd(0x21);
    write_cmd(0);
    write_cmd(127);
    // 页地址 0~7
    write_cmd(0x22);
    write_cmd(0);
    write_cmd(7);

    // 控制字节 0x40 + 1024 字节一次发完,比分页传输快一个数量级
    static uint8_t s_tx[SSD1306_BUFSIZE + 1];
    s_tx[0] = 0x40;
    memcpy(s_tx + 1, buf, SSD1306_BUFSIZE);
    esp_err_t err = i2c_master_transmit(s_dev, s_tx, SSD1306_BUFSIZE + 1, 200);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2c flush failed: %s", esp_err_to_name(err));
    }
}
