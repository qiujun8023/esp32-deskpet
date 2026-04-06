#pragma once
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

/* 差速驱动：left/right 范围 -255 ~ 255
   正值 = 向前，负值 = 向后，0 = 停止 */
void motor_set(int left, int right);

/* 简单指令（固定速度，供自主运动使用）*/
void motor_exec(motor_cmd_t cmd);
void motor_exec_timed(motor_cmd_t cmd, uint32_t duration_ms);

extern volatile bool g_manual_lock;
