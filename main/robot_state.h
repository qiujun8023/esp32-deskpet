#pragma once
#include <stdatomic.h>
#include <stdbool.h>

#include "display/robot_eyes.h"

/* 数值与 eye_mode_t 对齐,供 auto_move 任务和眼睛引擎共享 */
extern atomic_int robot_state_auto_mode;

void robot_state_set_auto_mode(int mode);
