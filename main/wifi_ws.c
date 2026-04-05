#include "wifi_ws.h"
#include "motor.h"
#include "display/robo_eyes.h"
#include "robot_state.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdio.h>

#define WIFI_SSID   "桌面机器人"
#define WIFI_CHAN   1
#define WIFI_MAX_STA 4

static const char *TAG = "wifi_ws";
static httpd_handle_t s_server = NULL;
static int s_ws_fd = -1;  // 当前 WebSocket 连接的文件描述符

/* ================================================================
 * 内嵌控制网页（HTML + CSS + JS，虚拟摇杆 + 情绪/模式按钮）
 * ================================================================ */
static const char s_html[] =
"<!DOCTYPE html><html lang='zh-CN'>"
"<head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>"
"<title>桌面机器人</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{background:#0a0e1a;color:#00ffe1;font-family:Arial,sans-serif;"
"display:flex;flex-direction:column;align-items:center;justify-content:center;"
"min-height:100vh;gap:20px;padding:16px}"
"h2{font-size:1.2rem;letter-spacing:3px;text-shadow:0 0 12px #00ffe1}"
"#joystick-area{position:relative;width:180px;height:180px;"
"border-radius:50%;border:2px solid rgba(0,255,225,0.4);"
"background:rgba(0,255,225,0.05);touch-action:none}"
"#stick{position:absolute;width:60px;height:60px;border-radius:50%;"
"background:radial-gradient(circle,#00ffe1,#00b3a4);"
"box-shadow:0 0 16px #00ffe1;top:60px;left:60px;pointer-events:none}"
".btn-row{display:flex;gap:10px}"
".btn{border:none;border-radius:12px;padding:10px 18px;"
"font-size:1rem;font-weight:bold;cursor:pointer;"
"background:linear-gradient(145deg,#0ff,#00b3a4);color:#000;opacity:.7;transition:.15s}"
".btn.active,.btn:active{opacity:1;box-shadow:0 0 14px #00ffe1}"
".mood-row{display:flex;gap:10px;font-size:1.4rem}"
".mood-btn{background:none;border:2px solid rgba(0,255,225,.3);"
"border-radius:10px;padding:8px 14px;font-size:1.4rem;cursor:pointer;transition:.15s}"
".mood-btn.active{border-color:#00ffe1;background:rgba(0,255,225,.15);"
"box-shadow:0 0 10px #00ffe1}"
"#status{font-size:.75rem;opacity:.5;letter-spacing:1px}"
"</style></head><body>"
"<h2>桌面机器人</h2>"
"<div id='joystick-area'><div id='stick'></div></div>"
"<div class='btn-row'>"
"<button class='btn active' id='m0' onclick='setMode(0)'>睡眠</button>"
"<button class='btn' id='m1' onclick='setMode(1)'>摆动</button>"
"<button class='btn' id='m2' onclick='setMode(2)'>好奇</button>"
"</div>"
"<div class='mood-row'>"
"<button class='mood-btn active' id='e0' onclick='setEmo(\"default\")'>😐</button>"
"<button class='mood-btn' id='e1' onclick='setEmo(\"happy\")'>😄</button>"
"<button class='mood-btn' id='e2' onclick='setEmo(\"angry\")'>😡</button>"
"<button class='mood-btn' id='e3' onclick='setEmo(\"tired\")'>😴</button>"
"</div>"
"<div id='status'>连接中...</div>"
"<script>"
"var ws,joy=document.getElementById('joystick-area'),"
"stick=document.getElementById('stick'),"
"area=joy.getBoundingClientRect(),"
"R=90,r=30,dragging=false,ox=0,oy=0,tid;"
""
"function connect(){"
"  ws=new WebSocket('ws://'+location.host+'/ws');"
"  ws.onopen=function(){document.getElementById('status').textContent='已连接';}"
"  ws.onclose=function(){document.getElementById('status').textContent='已断开，重连中...';setTimeout(connect,2000);}"
"  ws.onerror=function(){ws.close();}"
"}"
"connect();"
""
"function send(obj){"
"  if(ws&&ws.readyState===1)ws.send(JSON.stringify(obj));"
"}"
""
"function moveTo(x,y){"
"  var d=Math.sqrt(x*x+y*y);"
"  if(d>R-r){x=x/d*(R-r);y=y/d*(R-r);}"
"  stick.style.left=(R-r+x)+'px';"
"  stick.style.top=(R-r+y)+'px';"
"  send({j:[x/(R-r),y/(R-r)]});"
"}"
""
"function resetStick(){"
"  stick.style.left=(R-r)+'px';"
"  stick.style.top=(R-r)+'px';"
"  send({j:[0,0]});"
"}"
""
"joy.addEventListener('touchstart',function(e){"
"  e.preventDefault();dragging=true;"
"  area=joy.getBoundingClientRect();"
"  ox=area.left+R;oy=area.top+R;"
"},{ passive:false });"
""
"joy.addEventListener('touchmove',function(e){"
"  e.preventDefault();"
"  if(!dragging)return;"
"  var t=e.touches[0];"
"  moveTo(t.clientX-ox,t.clientY-oy);"
"},{ passive:false });"
""
"joy.addEventListener('touchend',function(e){"
"  e.preventDefault();dragging=false;resetStick();"
"},{ passive:false });"
""
"joy.addEventListener('mousedown',function(e){"
"  dragging=true;"
"  area=joy.getBoundingClientRect();"
"  ox=area.left+R;oy=area.top+R;"
"  moveTo(e.clientX-ox,e.clientY-oy);"
"});"
"window.addEventListener('mousemove',function(e){if(dragging)moveTo(e.clientX-ox,e.clientY-oy);});"
"window.addEventListener('mouseup',function(){if(dragging){dragging=false;resetStick();}});"
""
"var curMode=0;"
"function setMode(m){"
"  curMode=m;"
"  for(var i=0;i<3;i++)document.getElementById('m'+i).classList.toggle('active',i===m);"
"  send({m:['off','soft','normal'][m]});"
"}"
""
"var emos=['default','happy','angry','tired'];"
"var curEmo=0;"
"function setEmo(e){"
"  var idx=emos.indexOf(e);curEmo=idx>=0?idx:0;"
"  for(var i=0;i<4;i++)document.getElementById('e'+i).classList.toggle('active',i===curEmo);"
"  send({e:e});"
"}"
"</script></body></html>";

/* ================================================================
 * WebSocket 消息解析
 * ================================================================ */
static void parse_ws_msg(const char *data, size_t len)
{
    /* 简单 JSON 解析，不引入外部库 */
    /* 格式：{"j":[x,y]}  {"m":"off|soft|normal"}  {"e":"default|happy|angry|tired"} */

    if (strstr(data, "\"j\"")) {
        float jx = 0, jy = 0;
        sscanf(strstr(data, ":[") + 2, "%f,%f", &jx, &jy);

        /* 死区：摇杆归中时停车 */
        float mag = jx * jx + jy * jy;
        if (mag < 0.02f) {  // 约 0.14 半径内视为归中
            g_manual_lock = false;
            motor_set(0, 0);
            return;
        }

        g_manual_lock = true;

        /* 差速驱动公式：
         *   jy < 0 = 上推（前进），jy > 0 = 下推（后退）
         *   left  = -jy + jx   右轮快 → 右转
         *   right = -jy - jx
         * 结果范围 [-1, 1]，映射到 [-255, 255]
         */
        float fl = -jy + jx;
        float fr = -jy - jx;

        /* 归一化：当斜推时两路之和超过 1，等比缩小保持方向正确 */
        float maxv = fl < 0 ? -fl : fl;
        if ((fr < 0 ? -fr : fr) > maxv) maxv = fr < 0 ? -fr : fr;
        if (maxv > 1.0f) { fl /= maxv; fr /= maxv; }

        motor_set((int)(fl * 255), (int)(fr * 255));
        return;
    }

    if (strstr(data, "\"m\"")) {
        if (strstr(data, "off"))         set_auto_mode(EYE_MODE_SLEEP);
        else if (strstr(data, "soft"))   set_auto_mode(EYE_MODE_SOFT);
        else if (strstr(data, "normal")) set_auto_mode(EYE_MODE_NORMAL);
        return;
    }

    if (strstr(data, "\"e\"")) {
        if (strstr(data, "happy"))   robo_eyes_set_mood(MOOD_HAPPY);
        else if (strstr(data, "angry"))  robo_eyes_set_mood(MOOD_ANGRY);
        else if (strstr(data, "tired"))  robo_eyes_set_mood(MOOD_TIRED);
        else                             robo_eyes_set_mood(MOOD_DEFAULT);
        return;
    }
}

/* ================================================================
 * HTTP/WebSocket 处理器
 * ================================================================ */
static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, s_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handle_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket 握手 */
        s_ws_fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket 连接建立, fd=%d", s_ws_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t pkt;
    uint8_t buf[128] = {0};
    memset(&pkt, 0, sizeof(pkt));
    pkt.payload = buf;
    pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) return ret;

    buf[pkt.len] = '\0';
    parse_ws_msg((char *)buf, pkt.len);
    return ESP_OK;
}

static esp_err_t handle_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://10.10.10.10/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ================================================================
 * 捕获门户 DNS 任务（将所有域名解析到 10.10.10.10）
 * ================================================================ */
static void dns_captive_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    uint8_t buf[512];
    struct sockaddr_in client;
    socklen_t clen = sizeof(client);

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&client, &clen);
        if (n < 12) continue;

        /* 构造 DNS 应答：把查询直接回答为 10.10.10.10 */
        uint8_t resp[512];
        memcpy(resp, buf, n);
        resp[2] = 0x81; resp[3] = 0x80;  // QR=1, AA=1, RCODE=0
        resp[6] = 0x00; resp[7] = 0x01;  // ANCOUNT=1

        /* 附加 Answer 段：指针到 Question，type A，TTL 60，IP */
        int pos = n;
        resp[pos++] = 0xC0; resp[pos++] = 0x0C;  // 名称指针
        resp[pos++] = 0x00; resp[pos++] = 0x01;  // type A
        resp[pos++] = 0x00; resp[pos++] = 0x01;  // class IN
        resp[pos++] = 0x00; resp[pos++] = 0x00;
        resp[pos++] = 0x00; resp[pos++] = 60;    // TTL=60
        resp[pos++] = 0x00; resp[pos++] = 0x04;  // RDLENGTH=4
        resp[pos++] = 10;  resp[pos++] = 10;
        resp[pos++] = 10;  resp[pos++] = 10;     // 10.10.10.10

        sendto(sock, resp, pos, 0, (struct sockaddr *)&client, clen);
    }
}

/* ================================================================
 * 初始化入口
 * ================================================================ */
void wifi_ws_init(void)
{
    /* NVS 初始化 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 事件循环 */
    esp_event_loop_create_default();

    /* WiFi AP 初始化 */
    esp_netif_init();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    /* 设置自定义 AP IP：10.10.10.10 */
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_ip_info_t ip_info = {
        .ip      = { .addr = ESP_IP4TOADDR(10, 10, 10, 10) },
        .netmask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) },
        .gw      = { .addr = ESP_IP4TOADDR(10, 10, 10, 10) },
    };
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_AP);

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid           = WIFI_SSID,
            .ssid_len       = 0,
            .channel        = WIFI_CHAN,
            .authmode       = WIFI_AUTH_OPEN,
            .max_connection = WIFI_MAX_STA,
        },
    };
    /* SSID 含中文，使用 memcpy 而非字符串字面量赋值 */
    memcpy(ap_cfg.ap.ssid, WIFI_SSID, strlen(WIFI_SSID) + 1);
    ap_cfg.ap.ssid_len = strlen(WIFI_SSID);

    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi AP 已启动: %s  IP: 10.10.10.10", WIFI_SSID);

    /* HTTP 服务器 */
    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.lru_purge_enable = true;
    httpd_start(&s_server, &hcfg);

    static const httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = handle_root,
    };
    static const httpd_uri_t uri_ws = {
        .uri = "/ws", .method = HTTP_GET, .handler = handle_ws,
        .is_websocket = true,
    };
    static const httpd_uri_t uri_catch = {
        .uri = "/*", .method = HTTP_GET, .handler = handle_redirect,
    };
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_ws);
    httpd_register_uri_handler(s_server, &uri_catch);

    /* DNS 捕获门户任务 */
    xTaskCreate(dns_captive_task, "dns_captive", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "HTTP + WebSocket 服务器已启动");
}
