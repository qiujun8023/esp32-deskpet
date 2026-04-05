#include "ssd1306.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>

#define I2C_SDA_PIN     8
#define I2C_SCL_PIN     9
#define I2C_SPEED_HZ    400000
#define SSD1306_ADDR    0x3C

static const char *TAG = "ssd1306";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

/* SSD1306 初始化命令序列 */
static const uint8_t s_init_cmds[] = {
    0xAE,       // 关闭显示
    0xD5, 0x80, // 设置时钟分频
    0xA8, 0x3F, // 设置多路复用比（64行）
    0xD3, 0x00, // 显示偏移 = 0
    0x40,       // 起始行 = 0
    0x8D, 0x14, // 开启电荷泵
    0x20, 0x00, // 水平寻址模式
    0xA1,       // 列地址映射（翻转）
    0xC8,       // 行扫描方向（翻转）
    0xDA, 0x12, // COM 引脚配置
    0x81, 0xCF, // 对比度
    0xD9, 0xF1, // 预充电周期
    0xDB, 0x40, // VCOMH 电压
    0xA4,       // 输出跟随 RAM
    0xA6,       // 正常显示（非反转）
    0xAF,       // 开启显示
};

static void write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};  // 0x00 = Co=0, D/C=0（命令）
    i2c_master_transmit(s_dev, buf, 2, 100);
}

void ssd1306_init(void)
{
    /* 初始化 I2C 总线 */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    /* 添加 SSD1306 设备 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SSD1306_ADDR,
        .scl_speed_hz = I2C_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));

    /* 发送初始化命令 */
    for (int i = 0; i < (int)sizeof(s_init_cmds); i++) {
        write_cmd(s_init_cmds[i]);
    }

    /* 清屏 */
    static uint8_t blank[SSD1306_BUFSIZE] = {0};
    ssd1306_flush(blank);

    ESP_LOGI(TAG, "SSD1306 初始化完成");
}

void ssd1306_flush(const uint8_t *buf)
{
    /* 设置列地址范围 0~127 */
    write_cmd(0x21); write_cmd(0); write_cmd(127);
    /* 设置页地址范围 0~7 */
    write_cmd(0x22); write_cmd(0); write_cmd(7);

    /* 发送数据：控制字节 0x40（D/C=1）+ 1024 字节帧缓冲，合并为一次 I2C 传输 */
    static uint8_t s_tx[SSD1306_BUFSIZE + 1];
    s_tx[0] = 0x40;
    memcpy(s_tx + 1, buf, SSD1306_BUFSIZE);
    i2c_master_transmit(s_dev, s_tx, SSD1306_BUFSIZE + 1, 200);
}
