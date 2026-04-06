#include "display/robo_eyes.h"
#include "display/ssd1306.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motor.h"
#include "robot_state.h"
#include "wifi_ws.h"

static const char* TAG = "main";

/* 全局自主运动模式（由 wifi_ws 通过 set_auto_mode() 修改）*/
volatile int g_auto_mode = EYE_MODE_NORMAL;

void set_auto_mode(int mode) {
    g_auto_mode = mode;
    robo_eyes_set_mode((eye_mode_t)mode);
}

/* ---- 自主运动任务（Core 0，低优先级）---- */
static void task_auto_move(void* arg) {
    /* 随机指令表 */
    static const motor_cmd_t cmds[] = {
        MOTOR_STOP, MOTOR_FWD, MOTOR_FWD, MOTOR_BWD, MOTOR_LEFT, MOTOR_RIGHT, MOTOR_LEFT, MOTOR_RIGHT,
    };
    const int ncmds = sizeof(cmds) / sizeof(cmds[0]);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(40));

        if (g_manual_lock) continue;

        int mode = g_auto_mode;
        if (mode == EYE_MODE_SLEEP) {
            motor_exec(MOTOR_STOP);
            continue;
        }

        /* SOFT：低频率小幅运动（约每 5 秒一次）
           NORMAL：较高频率（约每 2 秒一次）*/
        uint32_t prob = (mode == EYE_MODE_SOFT) ? 125 : 50;
        if ((esp_random() % prob) == 0) {
            int idx = esp_random() % ncmds;
            int dur = (mode == EYE_MODE_SOFT) ? 50 + (esp_random() % 150)   // 50~200ms
                                              : 80 + (esp_random() % 400);  // 80~480ms
            motor_exec_timed(cmds[idx], dur);
        }
    }
}

/* ---- 显示任务（Core 0，高优先级）---- */
static void task_display(void* arg) {
    while (1) {
        robo_eyes_update();
        vTaskDelay(pdMS_TO_TICKS(20));  // ~50fps
    }
}

/* ---- 应用入口 ---- */
void app_main(void) {
    ESP_LOGI(TAG, "===== deskpet starting =====");

    motor_init();
    ssd1306_init();
    robo_eyes_init();
    wifi_ws_init();

    /* ESP32-C3 single core, both tasks run on Core 0 */
    xTaskCreatePinnedToCore(task_display, "display", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_auto_move, "auto_move", 2048, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "===== system ready =====");
}
