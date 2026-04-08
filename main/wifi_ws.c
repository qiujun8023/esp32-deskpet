#include "wifi_ws.h"

#include <stdio.h>
#include <string.h>

#include "display/robo_eyes.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
#include "motor.h"
#include "nvs_flash.h"
#include "robot_state.h"

#define WIFI_SSID    "ESP32-DeskPet"
#define WIFI_CHAN    6
#define WIFI_MAX_STA 4

static const char*    TAG      = "wifi_ws";
static httpd_handle_t s_server = NULL;

static const char s_html[] =
    "<!DOCTYPE html><html lang='zh-CN'>"
    "<head><meta charset='UTF-8'>"
    "<meta name='viewport' "
    "content='width=device-width,initial-scale=1,user-scalable=no'>"
    "<title>桌面机器人</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{background:#0a0e1a;color:#00ffe1;font-family:Arial,sans-serif;"
    "display:flex;flex-direction:column;align-items:center;justify-content:"
    "center;"
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
    "background:linear-gradient(145deg,#0ff,#00b3a4);color:#000;opacity:.7;"
    "transition:.15s}"
    ".btn.active,.btn:active{opacity:1;box-shadow:0 0 14px #00ffe1}"
    ".mood-row{display:flex;gap:10px;font-size:1.4rem}"
    ".mood-btn{background:none;border:2px solid rgba(0,255,225,.3);"
    "border-radius:10px;padding:8px "
    "14px;font-size:1.4rem;cursor:pointer;transition:.15s}"
    ".mood-btn.active{border-color:#00ffe1;background:rgba(0,255,225,.15);"
    "box-shadow:0 0 10px #00ffe1}"
    "#status{font-size:.75rem;opacity:.5;letter-spacing:1px}"
    "</style></head><body>"
    "<h2>桌面机器人</h2>"
    "<div id='joystick-area'><div id='stick'></div></div>"
    "<div class='btn-row'>"
    "<button class='btn' id='m0' onclick='setMode(0)'>睡眠</button>"
    "<button class='btn' id='m1' onclick='setMode(1)'>摆动</button>"
    "<button class='btn active' id='m2' onclick='setMode(2)'>好奇</button>"
    "</div>"
    "<div class='mood-row'>"
    "<button class='mood-btn active' id='e0' "
    "onclick='setEmo(\"default\")'>😐</button>"
    "<button class='mood-btn' id='e1' onclick='setEmo(\"happy\")'>😄</button>"
    "<button class='mood-btn' id='e2' onclick='setEmo(\"angry\")'>😡</button>"
    "<button class='mood-btn' id='e3' onclick='setEmo(\"tired\")'>😴</button>"
    "</div>"
    "<div id='status'>连接中...</div>"
    "<script>"
    "var ws,joy=document.getElementById('joystick-area'),"
    "stick=document.getElementById('stick'),"
    "area=joy.getBoundingClientRect(),"
    "R=90,r=30,dragging=false,ox=0,oy=0;"
    ""
    "function connect(){"
    "  ws=new WebSocket('ws://'+location.host+'/ws');"
    "  "
    "ws.onopen=function(){document.getElementById('status').textContent='"
    "已连接';};"
    "  "
    "ws.onclose=function(){document.getElementById('status').textContent='"
    "已断开，重连中...';setTimeout(connect,2000);};"
    "  ws.onerror=function(){ws.close();};"
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
    "window.addEventListener('mousemove',function(e){if(dragging)moveTo(e."
    "clientX-ox,e.clientY-oy);});"
    "window.addEventListener('mouseup',function(){if(dragging){dragging=false;"
    "resetStick();}});"
    ""
    "var curMode=2;"
    "function setMode(m){"
    "  curMode=m;"
    "  for(var "
    "i=0;i<3;i++)document.getElementById('m'+i).classList.toggle('active',i==="
    "m);"
    "  send({m:['off','soft','normal'][m]});"
    "}"
    ""
    "var emos=['default','happy','angry','tired'];"
    "var curEmo=0;"
    "function setEmo(e){"
    "  var idx=emos.indexOf(e);curEmo=idx>=0?idx:0;"
    "  for(var "
    "i=0;i<4;i++)document.getElementById('e'+i).classList.toggle('active',i==="
    "curEmo);"
    "  send({e:e});"
    "}"
    "</script></body></html>";

// 协议格式：{"j":[x,y]}  {"m":"off|soft|normal"}  {"e":"default|happy|angry|tired"}
// 差速驱动：jy<0 上推(前进)，jy>0 下推(后退)
// left = -jy + jx，right = -jy - jx，结果归一化到 [-1,1] 再映射 [-255,255]
static void parse_cmd(const char* data) {
    if (strstr(data, "\"j\"")) {
        float       jx = 0, jy = 0;
        const char* bracket = strstr(data, ":[");
        if (bracket) sscanf(bracket + 2, "%f,%f", &jx, &jy);

        if (jx * jx + jy * jy < 0.02f) {  // 死区约 0.14 半径
            g_manual_lock = false;
            motor_set(0, 0);
            return;
        }

        g_manual_lock = true;

        float fl   = -jy + jx;
        float fr   = -jy - jx;
        float maxv = fl < 0 ? -fl : fl;
        if ((fr < 0 ? -fr : fr) > maxv) maxv = fr < 0 ? -fr : fr;
        if (maxv > 1.0f) {
            fl /= maxv;
            fr /= maxv;
        }

        motor_set((int)(fl * 255), (int)(fr * 255));
        return;
    }

    if (strstr(data, "\"m\"")) {
        if (strstr(data, "off"))
            set_auto_mode(EYE_MODE_SLEEP);
        else if (strstr(data, "soft"))
            set_auto_mode(EYE_MODE_SOFT);
        else if (strstr(data, "normal"))
            set_auto_mode(EYE_MODE_NORMAL);
        return;
    }

    if (strstr(data, "\"e\"")) {
        if (strstr(data, "happy"))
            robo_eyes_set_mood(MOOD_HAPPY);
        else if (strstr(data, "angry"))
            robo_eyes_set_mood(MOOD_ANGRY);
        else if (strstr(data, "tired"))
            robo_eyes_set_mood(MOOD_TIRED);
        else
            robo_eyes_set_mood(MOOD_DEFAULT);
        return;
    }
}

static esp_err_t handle_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, s_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handle_ws(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "websocket connected, fd=%d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t pkt;
    uint8_t          buf[128] = {0};
    memset(&pkt, 0, sizeof(pkt));
    pkt.payload = buf;
    pkt.type    = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) return ret;

    buf[pkt.len] = '\0';
    parse_cmd((char*)buf);
    return ESP_OK;
}

static esp_err_t handle_redirect(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, s_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void dns_captive_task(void* arg) {
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket create failed");
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "dns socket bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t            buf[512];
    struct sockaddr_in client;
    socklen_t          clen = sizeof(client);

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&client, &clen);
        if (n < 12) continue;

        // Answer: 名称指针 → type A → class IN → TTL 60 → 10.10.10.10
        uint8_t resp[512];
        memcpy(resp, buf, n);
        resp[2] = 0x81;
        resp[3] = 0x80;  // QR=1 AA=1 RCODE=0
        resp[6] = 0x00;
        resp[7] = 0x01;  // ANCOUNT=1

        int pos     = n;
        resp[pos++] = 0xC0;
        resp[pos++] = 0x0C;
        resp[pos++] = 0x00;
        resp[pos++] = 0x01;
        resp[pos++] = 0x00;
        resp[pos++] = 0x01;
        resp[pos++] = 0x00;
        resp[pos++] = 0x00;
        resp[pos++] = 0x00;
        resp[pos++] = 60;
        resp[pos++] = 0x00;
        resp[pos++] = 0x04;
        resp[pos++] = 10;
        resp[pos++] = 10;
        resp[pos++] = 10;
        resp[pos++] = 10;

        sendto(sock, resp, pos, 0, (struct sockaddr*)&client, clen);
    }
}

static void wifi_ap_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    (void)arg;
    (void)base;

    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)data;
        ESP_LOGI(TAG, "station connected: MAC " MACSTR ", AID=%d", MAC2STR(event->mac), event->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)data;
        ESP_LOGI(TAG, "station disconnected: MAC " MACSTR ", AID=%d, reason=%d", MAC2STR(event->mac), event->aid,
                 event->reason);
    }
}

void wifi_ws_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &wifi_ap_event_handler,
                                                        NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED,
                                                        &wifi_ap_event_handler, NULL, NULL));

    wifi_config_t ap_cfg = {
        .ap =
            {
                .ssid_len        = strlen(WIFI_SSID),
                .channel         = WIFI_CHAN,
                .authmode        = WIFI_AUTH_OPEN,
                .max_connection  = WIFI_MAX_STA,
                .ssid_hidden     = 0,
                .beacon_interval = 100,
            },
    };
    strcpy((char*)ap_cfg.ap.ssid, WIFI_SSID);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    // HT20 避免 HT40 信道协商导致连接失败
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));
    // 手动设置国家码，避免 AUTO 策略动态降低发射功率
    wifi_country_t country = {
        .cc     = "CN",
        .schan  = 1,
        .nchan  = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    ESP_ERROR_CHECK(esp_wifi_start());

    // IP/DHCP 必须在 esp_wifi_start() 之后配置，否则会被内部默认处理器覆盖
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));

    esp_netif_ip_info_t ip_info = {
        .ip      = {.addr = ESP_IP4TOADDR(10, 10, 10, 10)},
        .netmask = {.addr = ESP_IP4TOADDR(255, 255, 255, 0)},
        .gw      = {.addr = ESP_IP4TOADDR(10, 10, 10, 10)},
    };
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));

    esp_netif_dns_info_t dns_info = {
        .ip.u_addr.ip4.addr = ESP_IP4TOADDR(10, 10, 10, 10),
        .ip.type            = IPADDR_TYPE_V4,
    };
    ESP_ERROR_CHECK(esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info));

    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(78));

    ESP_LOGI(TAG, "wifi ap started: ssid='%s', channel=%d, ip=10.10.10.10", WIFI_SSID, WIFI_CHAN);

    httpd_config_t hcfg   = HTTPD_DEFAULT_CONFIG();
    hcfg.lru_purge_enable = true;
    hcfg.max_uri_handlers = 8;
    hcfg.uri_match_fn     = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&s_server, &hcfg));

    static const httpd_uri_t uri_root = {
        .uri     = "/",
        .method  = HTTP_GET,
        .handler = handle_root,
    };
    static const httpd_uri_t uri_ws = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = handle_ws,
        .is_websocket = true,
    };
    static const httpd_uri_t uri_catch = {
        .uri     = "/*",
        .method  = HTTP_GET,
        .handler = handle_redirect,
    };
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_ws);
    httpd_register_uri_handler(s_server, &uri_catch);

    xTaskCreate(dns_captive_task, "dns_captive", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "http server started");
}
