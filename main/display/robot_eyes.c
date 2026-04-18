#include "robot_eyes.h"

#include <stdatomic.h>

#include "draw.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "ssd1306.h"

#define W SSD1306_WIDTH
#define H SSD1306_HEIGHT

#define EYE_W_DEF 36
#define EYE_H_DEF 36
#define EYE_R_DEF 8
#define EYE_GAP   10

/* 居中布局:两眼总宽 36+10+36=82,起点 x=(128-82)/2=23 */
#define EYE_L_X_DEF 23
#define EYE_R_X_DEF (EYE_L_X_DEF + EYE_W_DEF + EYE_GAP)
#define EYE_Y_DEF   ((H - EYE_H_DEF) / 2.0f)

#define EYE_Y_MIN 0
#define EYE_Y_MAX (H - EYE_H_DEF)
/* 以左眼 x 为漫游基准,右眼保持 EYE_GAP 同步偏移 */
#define EYE_X_MIN 0
#define EYE_X_MAX (W - EYE_W_DEF * 2 - EYE_GAP)

#define LERP_FAST 0.25f
#define LERP_SLOW 0.15f

typedef struct {
    float cx, cy;
    float tx, ty;
    float ch, th;
} eye_t;

static eye_t          s_eye_l, s_eye_r;
static _Atomic mood_t s_mood = MOOD_DEFAULT;
static _Atomic eye_mode_t s_mode = EYE_MODE_NORMAL;

// 0=张开等待 1=闭合中 2=张开中
static int     s_blink_state   = 0;
static int64_t s_blink_next_us = 0;

static int64_t s_idle_next_us = 0;

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}
static int rand_range(int lo, int hi) {
    return lo + (int)(esp_random() % (hi - lo + 1));
}
static int64_t now_us(void) {
    return esp_timer_get_time();
}

void robot_eyes_init(void) {
    s_eye_l.cx = s_eye_l.tx = EYE_L_X_DEF;
    s_eye_l.cy = s_eye_l.ty = EYE_Y_DEF;
    // 初始高度置 1 制造开机"睁眼"动画
    s_eye_l.ch              = 1.0f;
    s_eye_l.th              = (float)EYE_H_DEF;

    s_eye_r.cx = s_eye_r.tx = EYE_R_X_DEF;
    s_eye_r.cy = s_eye_r.ty = EYE_Y_DEF;
    s_eye_r.ch              = 1.0f;
    s_eye_r.th              = (float)EYE_H_DEF;

    s_blink_next_us = now_us() + 2000000LL;
    s_idle_next_us  = now_us() + 1000000LL;
}

void robot_eyes_set_mood(mood_t mood) {
    atomic_store(&s_mood, mood);
}
void robot_eyes_set_mode(eye_mode_t m) {
    atomic_store(&s_mode, m);
}

static void trigger_blink(void) {
    s_eye_l.th    = 1.0f;
    s_eye_r.th    = 1.0f;
    s_blink_state = 1;
}

static void trigger_idle(void) {
    int new_x  = rand_range(EYE_X_MIN, EYE_X_MAX);
    int new_y  = rand_range(EYE_Y_MIN, EYE_Y_MAX);
    s_eye_l.tx = new_x;
    s_eye_l.ty = new_y;
    s_eye_r.tx = new_x + EYE_W_DEF + EYE_GAP;
    s_eye_r.ty = new_y;
}

void robot_eyes_update(void) {
    int64_t    t    = now_us();
    eye_mode_t mode = atomic_load(&s_mode);
    mood_t     mood = atomic_load(&s_mood);

    if (s_blink_state == 0) {
        if (t >= s_blink_next_us) {
            trigger_blink();
        }
    } else if (s_blink_state == 1) {
        // 插值渐近,不等严格归零,否则下一步目标切回默认高度会让闭合动画卡住
        if (s_eye_l.ch < 3.0f) {
            s_eye_l.th    = EYE_H_DEF;
            s_eye_r.th    = EYE_H_DEF;
            s_blink_state = 2;
        }
    } else {
        if (s_eye_l.ch > EYE_H_DEF - 3) {
            s_blink_state = 0;
            s_blink_next_us = t + (int64_t)rand_range(3000, 6000) * 1000LL;
        }
    }

    if (mode != EYE_MODE_SLEEP) {
        if (t >= s_idle_next_us) {
            trigger_idle();
            int interval_ms = (mode == EYE_MODE_SOFT) ? rand_range(3000, 6000) : rand_range(1500, 4000);
            s_idle_next_us  = t + (int64_t)interval_ms * 1000LL;
        }
    } else {
        s_eye_l.tx = EYE_L_X_DEF;
        s_eye_l.ty = EYE_Y_DEF;
        s_eye_r.tx = EYE_R_X_DEF;
        s_eye_r.ty = EYE_Y_DEF;
    }

    // 睡眠模式用慢插值,视觉上更像放松游离
    float sp   = (mode == EYE_MODE_SLEEP) ? LERP_SLOW : LERP_FAST;
    s_eye_l.cx = lerpf(s_eye_l.cx, s_eye_l.tx, sp);
    s_eye_l.cy = lerpf(s_eye_l.cy, s_eye_l.ty, sp);
    s_eye_l.ch = lerpf(s_eye_l.ch, s_eye_l.th, LERP_FAST);

    s_eye_r.cx = lerpf(s_eye_r.cx, s_eye_r.tx, sp);
    s_eye_r.cy = lerpf(s_eye_r.cy, s_eye_r.ty, sp);
    s_eye_r.ch = lerpf(s_eye_r.ch, s_eye_r.th, LERP_FAST);

    if (s_blink_state == 0) {
        s_eye_l.th = EYE_H_DEF;
        s_eye_r.th = EYE_H_DEF;
    }

    draw_clear();

    int lh_i = (int)s_eye_l.ch;
    int rh_i = (int)s_eye_r.ch;
    if (lh_i < 1)
        lh_i = 1;
    if (rh_i < 1)
        rh_i = 1;

    int lx = (int)s_eye_l.cx;
    int rx = (int)s_eye_r.cx;
    // 高度变化时 y 反向补偿,让眨眼看起来是上下眼皮同时向中线收拢
    int ly = (int)(s_eye_l.cy + (EYE_H_DEF - lh_i) / 2.0f);
    int ry = (int)(s_eye_r.cy + (EYE_H_DEF - rh_i) / 2.0f);

    draw_fill_round_rect(lx, ly, EYE_W_DEF, lh_i, EYE_R_DEF, 1);
    draw_fill_round_rect(rx, ry, EYE_W_DEF, rh_i, EYE_R_DEF, 1);

    // 眨眼过程中跳过情绪遮罩,避免两层动画叠加出现闪烁
    if (s_blink_state == 0 && lh_i >= EYE_H_DEF - 3) {
        int lid = lh_i / 2;
        switch (mood) {
            case MOOD_TIRED:
                // 左右对称的外高内低三角,模拟眼皮下垂
                draw_fill_triangle(lx, ly - 1, lx + EYE_W_DEF, ly - 1, lx, ly + lid - 1, 0);
                draw_fill_triangle(rx, ry - 1, rx + EYE_W_DEF, ry - 1, rx + EYE_W_DEF, ry + lid - 1, 0);
                break;
            case MOOD_ANGRY:
                // 与 tired 三角方向相反,形成眉头内收
                draw_fill_triangle(lx, ly - 1, lx + EYE_W_DEF, ly - 1, lx + EYE_W_DEF, ly + lid - 1, 0);
                draw_fill_triangle(rx, ry - 1, rx + EYE_W_DEF, ry - 1, rx, ry + lid - 1, 0);
                break;
            case MOOD_HAPPY:
                // 用大圆角矩形在下半部做"擦除",保留顶部弧线成笑眼
                draw_fill_round_rect(lx - 1, ly + lh_i - lid + 1, EYE_W_DEF + 2, EYE_H_DEF, EYE_R_DEF, 0);
                draw_fill_round_rect(rx - 1, ry + rh_i - lid + 1, EYE_W_DEF + 2, EYE_H_DEF, EYE_R_DEF, 0);
                break;
            default:
                break;
        }
    }

    ssd1306_flush(draw_framebuf);
}
