#pragma once
#include <stdbool.h>
#include "display/robo_eyes.h"

/* 全局自主运动模式（0=睡眠 1=摆动 2=好奇）*/
extern volatile int g_auto_mode;

/* 切换运动模式（同步更新眼睛动画引擎）*/
void set_auto_mode(int mode);
