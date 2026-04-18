#include "net/http_server.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "display/robot_eyes.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "motor.h"
#include "robot_state.h"

static const char* TAG = "http";

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

/* captive portal 已授权客户端 IP 环形缓冲:已授权者访问探测 URL 直接返回"有网",不再触发弹窗 */
static uint32_t          s_portal_done[CONFIG_ESP_MAX_STA_CONN];
static uint8_t           s_portal_count = 0;
static uint8_t           s_portal_head  = 0;
static SemaphoreHandle_t s_portal_lock  = NULL;

static uint32_t get_client_ip(httpd_req_t* req) {
    int                fd = httpd_req_to_sockfd(req);
    struct sockaddr_in addr;
    socklen_t          len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr*)&addr, &len) != 0)
        return 0;
    return addr.sin_addr.s_addr;
}

static bool portal_is_done_locked(uint32_t ip) {
    for (int i = 0; i < s_portal_count; i++) {
        if (s_portal_done[i] == ip)
            return true;
    }
    return false;
}

static bool portal_is_done(uint32_t ip) {
    if (!s_portal_lock) return false;
    xSemaphoreTake(s_portal_lock, portMAX_DELAY);
    bool r = portal_is_done_locked(ip);
    xSemaphoreGive(s_portal_lock);
    return r;
}

/* 满后最老条目被新 IP 覆盖,避免长时间运行后拒绝新客户端 */
static void portal_mark_done(uint32_t ip) {
    if (!ip || !s_portal_lock) return;
    xSemaphoreTake(s_portal_lock, portMAX_DELAY);
    if (!portal_is_done_locked(ip)) {
        s_portal_done[s_portal_head] = ip;
        s_portal_head                = (s_portal_head + 1) % CONFIG_ESP_MAX_STA_CONN;
        if (s_portal_count < CONFIG_ESP_MAX_STA_CONN)
            s_portal_count++;
    }
    xSemaphoreGive(s_portal_lock);
}

/* WebSocket 协议:
 *   {"j":[x,y]}  摇杆,x/y 约 -1~1
 *   {"m":"off|soft|normal"}  自主运动模式
 *   {"e":"default|happy|angry|tired"}  情绪
 */
static void parse_joystick(const cJSON* arr) {
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 2)
        return;
    float jx = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 0));
    float jy = (float)cJSON_GetNumberValue(cJSON_GetArrayItem(arr, 1));

    // 死区:抖动范围内视为松手,释放手动锁让自主运动接管
    if (jx * jx + jy * jy < 0.02f) {
        atomic_store(&motor_manual_lock, false);
        motor_set(0, 0);
        return;
    }

    atomic_store(&motor_manual_lock, true);

    // 差速驱动:前推(jy<0) 时两轮同速前进,横向 jx 形成左右差值,再按最大绝对值归一化
    float fl   = -jy + jx;
    float fr   = -jy - jx;
    float maxv = fl < 0 ? -fl : fl;
    if ((fr < 0 ? -fr : fr) > maxv)
        maxv = fr < 0 ? -fr : fr;
    if (maxv > 1.0f) {
        fl /= maxv;
        fr /= maxv;
    }
    motor_set((int)(fl * 255), (int)(fr * 255));
}

static void parse_mode(const char* s) {
    if (!s)
        return;
    if (!strcmp(s, "off"))
        robot_state_set_auto_mode(EYE_MODE_SLEEP);
    else if (!strcmp(s, "soft"))
        robot_state_set_auto_mode(EYE_MODE_SOFT);
    else if (!strcmp(s, "normal"))
        robot_state_set_auto_mode(EYE_MODE_NORMAL);
}

static void parse_emotion(const char* s) {
    if (!s)
        return;
    if (!strcmp(s, "happy"))
        robot_eyes_set_mood(MOOD_HAPPY);
    else if (!strcmp(s, "angry"))
        robot_eyes_set_mood(MOOD_ANGRY);
    else if (!strcmp(s, "tired"))
        robot_eyes_set_mood(MOOD_TIRED);
    else
        robot_eyes_set_mood(MOOD_DEFAULT);
}

static void parse_cmd(const char* data) {
    cJSON* root = cJSON_Parse(data);
    if (!root)
        return;

    cJSON* j = cJSON_GetObjectItem(root, "j");
    if (j) {
        parse_joystick(j);
        cJSON_Delete(root);
        return;
    }
    cJSON* m = cJSON_GetObjectItem(root, "m");
    if (cJSON_IsString(m)) {
        parse_mode(m->valuestring);
        cJSON_Delete(root);
        return;
    }
    cJSON* e = cJSON_GetObjectItem(root, "e");
    if (cJSON_IsString(e)) {
        parse_emotion(e->valuestring);
    }
    cJSON_Delete(root);
}

static esp_err_t handle_index(httpd_req_t* req) {
    portal_mark_done(get_client_ip(req));
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char*)index_html_start, index_html_end - index_html_start);
}

static esp_err_t handle_ws(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "ws connected fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    // 先 len=0 探帧头,过滤非文本/超长帧,避免后续拷贝溢出
    httpd_ws_frame_t pkt = {.type = HTTPD_WS_TYPE_TEXT};
    esp_err_t        ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK)
        return ret;
    if (pkt.type != HTTPD_WS_TYPE_TEXT || pkt.len == 0 || pkt.len > 127)
        return ESP_OK;

    uint8_t buf[128] = {0};
    pkt.payload      = buf;
    ret              = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
    if (ret != ESP_OK)
        return ret;

    buf[pkt.len] = '\0';
    parse_cmd((char*)buf);
    return ESP_OK;
}

/* 已授权客户端对连通性探测返回各平台预期的"有网"响应,避免系统反复弹窗或切回移动数据 */
static esp_err_t reply_connectivity_success(httpd_req_t* req) {
    const char* uri = req->uri;

    // Android / Chrome
    if (strstr(uri, "generate_204") || strstr(uri, "gen_204")) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }
    // Apple iOS / macOS
    if (strstr(uri, "hotspot-detect.html")) {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_sendstr(req, "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    }
    // Windows
    if (strstr(uri, "connecttest.txt")) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Microsoft Connect Test");
    }
    if (strstr(uri, "ncsi.txt")) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Microsoft NCSI");
    }
    // Firefox
    if (strstr(uri, "canonical.html") || strstr(uri, "success.txt")) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "success\n");
    }

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_sendstr(req, "Redirect to captive portal");
}

static esp_err_t handle_404(httpd_req_t* req, httpd_err_code_t err) {
    (void)err;
    uint32_t ip = get_client_ip(req);
    if (ip && portal_is_done(ip)) {
        return reply_connectivity_success(req);
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    // iOS captive 检测要求 body 非空才会判定为需要登录并弹窗
    return httpd_resp_sendstr(req, "Redirect to captive portal");
}

void http_server_start(void) {
    if (!s_portal_lock) s_portal_lock = xSemaphoreCreateMutex();

    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 7;
    // 手机切屏等场景常留半死 socket,短超时让 LRU 尽快回收
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;

    // 重定向探测请求会刷大量 404,默认日志等级下噪声过大
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    httpd_handle_t srv = NULL;
    ESP_ERROR_CHECK(httpd_start(&srv, &cfg));

    httpd_uri_t u_index = {.uri = "/", .method = HTTP_GET, .handler = handle_index};
    httpd_uri_t u_ws    = {.uri = "/ws", .method = HTTP_GET, .handler = handle_ws, .is_websocket = true};

    httpd_register_uri_handler(srv, &u_index);
    httpd_register_uri_handler(srv, &u_ws);
    httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, handle_404);

    ESP_LOGI(TAG, "http server started");
}
