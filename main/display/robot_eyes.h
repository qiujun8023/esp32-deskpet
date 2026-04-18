#pragma once
#include <stdint.h>

typedef enum {
    MOOD_DEFAULT = 0,
    MOOD_HAPPY,
    MOOD_ANGRY,
    MOOD_TIRED,
} mood_t;

/* 数值对应 robot_state_auto_mode,HTTP 层直接以整数传入 */
typedef enum {
    EYE_MODE_SLEEP  = 0,
    EYE_MODE_SOFT   = 1,
    EYE_MODE_NORMAL = 2,
} eye_mode_t;

void robot_eyes_init(void);
void robot_eyes_set_mood(mood_t mood);
void robot_eyes_set_mode(eye_mode_t mode);
/* 每帧调用,约 20ms 一次,内部处理眨眼/漫游/绘制并 flush */
void robot_eyes_update(void);
