#include "display/robot_eyes.h"
#include "display/ssd1306.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motor.h"
#include "net/captive_dns.h"
#include "net/http_server.h"
#include "net/wifi_ap.h"
#include "nvs_flash.h"
#include "robot_state.h"

static const char* TAG = "main";

static void task_auto_move(void* arg) {
    static const motor_cmd_t cmds[] = {
        MOTOR_STOP, MOTOR_FWD, MOTOR_FWD, MOTOR_BWD, MOTOR_LEFT, MOTOR_RIGHT, MOTOR_LEFT, MOTOR_RIGHT,
    };
    const int ncmds = sizeof(cmds) / sizeof(cmds[0]);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(40));

        if (atomic_load(&motor_manual_lock))
            continue;

        int mode = atomic_load(&robot_state_auto_mode);
        if (mode == EYE_MODE_SLEEP) {
            motor_exec(MOTOR_STOP);
            continue;
        }

        /* 25ms tick 下按概率触发:SOFT 约 5s 一次,NORMAL 约 2s 一次 */
        uint32_t prob = (mode == EYE_MODE_SOFT) ? 125 : 50;
        if ((esp_random() % prob) == 0) {
            int idx = esp_random() % ncmds;
            int dur = (mode == EYE_MODE_SOFT) ? 50 + (esp_random() % 150) : 80 + (esp_random() % 400);
            motor_exec_timed(cmds[idx], dur);
        }
    }
}

static void task_display(void* arg) {
    while (1) {
        robot_eyes_update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "deskpet booting");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs partition corrupted, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    motor_init();
    ssd1306_init();
    robot_eyes_init();

    wifi_ap_start();
    captive_dns_start();
    http_server_start();

    xTaskCreatePinnedToCore(task_display, "display", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_auto_move, "auto_move", 3072, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "system ready");
}
