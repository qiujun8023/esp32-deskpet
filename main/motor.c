#include "motor.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define PIN_LF    0
#define PIN_LB    1
#define PIN_RF    2
#define PIN_RB    3
#define PIN_STBY  10

#define PWM_FREQ_HZ    20000              // 20kHz，电机无噪声
#define PWM_RES        LEDC_TIMER_8_BIT  // 分辨率 8-bit（0~255）
#define PWM_TIMER      LEDC_TIMER_0
#define PWM_MODE       LEDC_LOW_SPEED_MODE

#define CH_LF  LEDC_CHANNEL_0
#define CH_LB  LEDC_CHANNEL_1
#define CH_RF  LEDC_CHANNEL_2
#define CH_RB  LEDC_CHANNEL_3

/* 自主运动的固定速度（0~255）*/
#define AUTO_SPEED  180

static const char *TAG = "motor";
volatile bool g_manual_lock = false;
static esp_timer_handle_t s_stop_timer = NULL;

/* ---- 工具函数 ---- */
static inline int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void set_duty(ledc_channel_t ch, uint32_t duty)
{
    ledc_set_duty(PWM_MODE, ch, duty);
    ledc_update_duty(PWM_MODE, ch);
}

/* ---- 定时器回调：自动停车 ---- */
static void on_stop_timer(void *arg)
{
    motor_set(0, 0);
}

/* ---- 初始化 ---- */
void motor_init(void)
{
    /* STBY 引脚：普通 GPIO 输出，常高使能 N298N */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_STBY,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_STBY, 1);

    /* LEDC 定时器 */
    ledc_timer_config_t timer = {
        .speed_mode      = PWM_MODE,
        .duty_resolution = PWM_RES,
        .timer_num       = PWM_TIMER,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    /* 4 个 PWM 通道（LF / LB / RF / RB）*/
    ledc_channel_config_t chs[] = {
        {.gpio_num=PIN_LF, .channel=CH_LF},
        {.gpio_num=PIN_LB, .channel=CH_LB},
        {.gpio_num=PIN_RF, .channel=CH_RF},
        {.gpio_num=PIN_RB, .channel=CH_RB},
    };
    for (int i = 0; i < 4; i++) {
        chs[i].speed_mode   = PWM_MODE;
        chs[i].timer_sel    = PWM_TIMER;
        chs[i].duty         = 0;
        chs[i].hpoint       = 0;
        ledc_channel_config(&chs[i]);
    }

    /* 创建自动停车定时器 */
    esp_timer_create_args_t ta = {.callback = on_stop_timer, .name = "motor_stop"};
    esp_timer_create(&ta, &s_stop_timer);

    ESP_LOGI(TAG, "电机 PWM 初始化完成（20kHz, 8-bit）");
}

/* ---- 差速驱动核心 ---- */
void motor_set(int left, int right)
{
    left  = clamp(left,  -255, 255);
    right = clamp(right, -255, 255);

    /* 左轮 */
    if (left > 0) {
        set_duty(CH_LF, (uint32_t)left);
        set_duty(CH_LB, 0);
    } else if (left < 0) {
        set_duty(CH_LF, 0);
        set_duty(CH_LB, (uint32_t)(-left));
    } else {
        set_duty(CH_LF, 0);
        set_duty(CH_LB, 0);
    }

    /* 右轮 */
    if (right > 0) {
        set_duty(CH_RB, (uint32_t)right);
        set_duty(CH_RF, 0);
    } else if (right < 0) {
        set_duty(CH_RB, 0);
        set_duty(CH_RF, (uint32_t)(-right));
    } else {
        set_duty(CH_RB, 0);
        set_duty(CH_RF, 0);
    }
}

/* ---- 简单指令（自主运动用固定速度）---- */
void motor_exec(motor_cmd_t cmd)
{
    switch (cmd) {
    case MOTOR_STOP:  motor_set(0,           0);           break;
    case MOTOR_FWD:   motor_set(AUTO_SPEED,  AUTO_SPEED);  break;
    case MOTOR_BWD:   motor_set(-AUTO_SPEED, -AUTO_SPEED); break;
    case MOTOR_LEFT:  motor_set(-AUTO_SPEED, AUTO_SPEED);  break;
    case MOTOR_RIGHT: motor_set(AUTO_SPEED, -AUTO_SPEED);  break;
    }
}

void motor_exec_timed(motor_cmd_t cmd, uint32_t duration_ms)
{
    if (s_stop_timer) esp_timer_stop(s_stop_timer);
    motor_exec(cmd);
    if (duration_ms > 0 && s_stop_timer) {
        esp_timer_start_once(s_stop_timer, (uint64_t)duration_ms * 1000ULL);
    }
}
