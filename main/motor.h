#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MOTOR_STOP = 0,
    MOTOR_FWD,
    MOTOR_BWD,
    MOTOR_LEFT,
    MOTOR_RIGHT,
} motor_cmd_t;

void motor_init(void);

/* 差速驱动,left/right 取值 -255~255,正值前进,负值后退 */
void motor_set(int left, int right);

void motor_exec(motor_cmd_t cmd);
void motor_exec_timed(motor_cmd_t cmd, uint32_t duration_ms);

/* 手动控制期间由 HTTP 层置位,抑制自主运动任务抢控 */
extern atomic_bool motor_manual_lock;
