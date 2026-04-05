#pragma once
#include <stdint.h>

typedef enum {
    MOOD_DEFAULT = 0,
    MOOD_HAPPY,
    MOOD_ANGRY,
    MOOD_TIRED,
} mood_t;

/* 自主漫游模式，影响眼睛漫视频率 */
typedef enum {
    EYE_MODE_SLEEP  = 0,  // 关闭漫游
    EYE_MODE_SOFT   = 1,  // 慢频率漫游
    EYE_MODE_NORMAL = 2,  // 正常频率漫游
} eye_mode_t;

void     robo_eyes_init(void);
void     robo_eyes_set_mood(mood_t mood);
void     robo_eyes_set_mode(eye_mode_t mode);
void     robo_eyes_update(void);  // 每帧调用（~20ms）
