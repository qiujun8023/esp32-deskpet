#include "robo_eyes.h"
#include "draw.h"
#include "ssd1306.h"
#include "esp_timer.h"
#include "esp_random.h"
/* ---- 屏幕参数 ---- */
#define W   SSD1306_WIDTH   // 128
#define H   SSD1306_HEIGHT  // 64

/* ---- 单眼几何 ---- */
#define EYE_W_DEF   36
#define EYE_H_DEF   36
#define EYE_R_DEF    8
#define EYE_GAP     10  // 两眼间距

/* 两眼总宽 = 36+10+36 = 82，起始 x = (128-82)/2 = 23 */
#define EYE_L_X_DEF  23
#define EYE_R_X_DEF  (EYE_L_X_DEF + EYE_W_DEF + EYE_GAP)
#define EYE_Y_DEF    ((H - EYE_H_DEF) / 2)   // 14

/* 眼睛 y 方向可漫游范围 */
#define EYE_Y_MIN    0
#define EYE_Y_MAX    (H - EYE_H_DEF)
/* 眼睛 x 方向可漫游范围（以左眼 x 为基准，右眼跟随） */
#define EYE_X_MIN    0
#define EYE_X_MAX    (W - EYE_W_DEF*2 - EYE_GAP)

/* 插值平滑系数（值越大收敛越快）*/
#define LERP_FAST  0.25f
#define LERP_SLOW  0.15f

/* ---- 内部状态 ---- */
typedef struct {
    float cx, cy;   // 当前值
    float tx, ty;   // 目标值
    float ch, th;   // 当前/目标高度（用于眨眼）
} eye_t;

static eye_t s_eye_l, s_eye_r;
static mood_t     s_mood = MOOD_DEFAULT;
static eye_mode_t s_mode = EYE_MODE_NORMAL;

/* 眨眼状态 */
static int   s_blink_state = 0;  // 0=张开等待 1=闭合中 2=张开中
static int64_t s_blink_next_us = 0;

/* 漫游定时 */
static int64_t s_idle_next_us = 0;

/* ---- 工具 ---- */
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static int   rand_range(int lo, int hi) { return lo + (int)(esp_random() % (hi - lo + 1)); }
static int64_t now_us(void) { return esp_timer_get_time(); }

/* ---- 初始化 ---- */
void robo_eyes_init(void)
{
    s_eye_l.cx = s_eye_l.tx = EYE_L_X_DEF;
    s_eye_l.cy = s_eye_l.ty = EYE_Y_DEF;
    s_eye_l.ch = 1.0f;            // 从闭合开始
    s_eye_l.th = (float)EYE_H_DEF;

    s_eye_r.cx = s_eye_r.tx = EYE_R_X_DEF;
    s_eye_r.cy = s_eye_r.ty = EYE_Y_DEF;
    s_eye_r.ch = 1.0f;
    s_eye_r.th = (float)EYE_H_DEF;

    s_blink_next_us = now_us() + 2000000LL;  // 2秒后第一次眨眼
    s_idle_next_us  = now_us() + 1000000LL;
}

void robo_eyes_set_mood(mood_t mood)  { s_mood = mood; }
void robo_eyes_set_mode(eye_mode_t m) { s_mode = m; }

/* ---- 触发一次眨眼 ---- */
static void trigger_blink(void)
{
    s_eye_l.th = 1.0f;
    s_eye_r.th = 1.0f;
    s_blink_state = 1;
}

/* ---- 漫游：随机移动眼睛位置 ---- */
static void trigger_idle(void)
{
    int new_x = rand_range(EYE_X_MIN, EYE_X_MAX);
    int new_y = rand_range(EYE_Y_MIN, EYE_Y_MAX);
    s_eye_l.tx = new_x;
    s_eye_l.ty = new_y;
    s_eye_r.tx = new_x + EYE_W_DEF + EYE_GAP;
    s_eye_r.ty = new_y;
}

/* ---- 每帧更新 ---- */
void robo_eyes_update(void)
{
    int64_t t = now_us();

    /* --- 眨眼逻辑 --- */
    if (s_blink_state == 0) {
        if (t >= s_blink_next_us) {
            trigger_blink();
        }
    } else if (s_blink_state == 1) {
        /* 等待眼睛基本闭合（高度 < 3）*/
        if (s_eye_l.ch < 3.0f) {
            s_eye_l.th = EYE_H_DEF;
            s_eye_r.th = EYE_H_DEF;
            s_blink_state = 2;
        }
    } else {
        /* 等待眼睛基本恢复 */
        if (s_eye_l.ch > EYE_H_DEF - 3) {
            s_blink_state = 0;
            /* 下次眨眼时间：3~6秒 */
            s_blink_next_us = t + (int64_t)rand_range(3000, 6000) * 1000LL;
        }
    }

    /* --- 漫游逻辑 --- */
    if (s_mode != EYE_MODE_SLEEP) {
        if (t >= s_idle_next_us) {
            trigger_idle();
            int interval_ms = (s_mode == EYE_MODE_SOFT)
                              ? rand_range(3000, 6000)
                              : rand_range(1500, 4000);
            s_idle_next_us = t + (int64_t)interval_ms * 1000LL;
        }
    } else {
        /* 睡眠模式：回归中心 */
        s_eye_l.tx = EYE_L_X_DEF;
        s_eye_l.ty = EYE_Y_DEF;
        s_eye_r.tx = EYE_R_X_DEF;
        s_eye_r.ty = EYE_Y_DEF;
    }

    /* --- 插值平滑 --- */
    float sp = (s_mode == EYE_MODE_SLEEP) ? LERP_SLOW : LERP_FAST;
    s_eye_l.cx = lerpf(s_eye_l.cx, s_eye_l.tx, sp);
    s_eye_l.cy = lerpf(s_eye_l.cy, s_eye_l.ty, sp);
    s_eye_l.ch = lerpf(s_eye_l.ch, s_eye_l.th, LERP_FAST);

    s_eye_r.cx = lerpf(s_eye_r.cx, s_eye_r.tx, sp);
    s_eye_r.cy = lerpf(s_eye_r.cy, s_eye_r.ty, sp);
    s_eye_r.ch = lerpf(s_eye_r.ch, s_eye_r.th, LERP_FAST);

    /* 好奇模式：眼睛偏向边缘时，外眼增高 */
    float lh = EYE_H_DEF, rh = EYE_H_DEF;
    if (s_mode != EYE_MODE_SLEEP) {
        if (s_eye_l.tx <= EYE_X_MIN + 5)       lh = EYE_H_DEF + 8;
        if (s_eye_r.tx >= EYE_X_MAX + EYE_W_DEF + EYE_GAP - 5) rh = EYE_H_DEF + 8;
    }
    if (s_blink_state == 0) {
        s_eye_l.th = lh;
        s_eye_r.th = rh;
    }

    /* --- 绘制 --- */
    draw_clear();

    int lx = (int)s_eye_l.cx;
    int ly = (int)s_eye_l.cy;
    int lh_i = (int)s_eye_l.ch;
    int rx = (int)s_eye_r.cx;
    int ry = (int)s_eye_r.cy;
    int rh_i = (int)s_eye_r.ch;
    if (lh_i < 1) lh_i = 1;
    if (rh_i < 1) rh_i = 1;

    /* 眼睛主体（圆角矩形）*/
    draw_fill_round_rect(lx, ly, EYE_W_DEF, lh_i, EYE_R_DEF, 1);
    draw_fill_round_rect(rx, ry, EYE_W_DEF, rh_i, EYE_R_DEF, 1);

    /* 情绪眼皮（仅在眼睛完全张开时显示）*/
    if (s_blink_state == 0 && lh_i > EYE_H_DEF / 2) {
        int lid = lh_i / 2;  // 眼皮遮罩高度
        switch (s_mood) {
        case MOOD_TIRED:
            /* 三角形遮罩：从左上到右中（眼皮下垂）*/
            draw_fill_triangle(lx,           ly-1,
                               lx+EYE_W_DEF, ly-1,
                               lx,           ly+lid-1, 0);
            draw_fill_triangle(rx,           ry-1,
                               rx+EYE_W_DEF, ry-1,
                               rx+EYE_W_DEF, ry+lid-1, 0);
            break;
        case MOOD_ANGRY:
            /* 三角形遮罩：从右上到左中（眉头皱起）*/
            draw_fill_triangle(lx,           ly-1,
                               lx+EYE_W_DEF, ly-1,
                               lx+EYE_W_DEF, ly+lid-1, 0);
            draw_fill_triangle(rx,           ry-1,
                               rx+EYE_W_DEF, ry-1,
                               rx,           ry+lid-1, 0);
            break;
        case MOOD_HAPPY:
            /* 底部圆角矩形遮罩（眼睛下半变成笑弧）*/
            draw_fill_round_rect(lx-1, ly+lh_i-lid+1, EYE_W_DEF+2, EYE_H_DEF, EYE_R_DEF, 0);
            draw_fill_round_rect(rx-1, ry+rh_i-lid+1, EYE_W_DEF+2, EYE_H_DEF, EYE_R_DEF, 0);
            break;
        default:
            break;
        }
    }

    ssd1306_flush(g_framebuf);
}
