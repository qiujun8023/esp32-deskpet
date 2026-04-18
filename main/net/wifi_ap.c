#include "net/wifi_ap.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "motor.h"

static const char* TAG = "wifi_ap";

/* DHCP option 114 (RFC 8910):iOS 14+ / Android 11+ 据此自动弹出 captive portal */
static char s_portal_uri[] = "http://192.168.4.1/";

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* e = (wifi_event_ap_staconnected_t*)data;
        ESP_LOGI(TAG, "sta connected, mac=" MACSTR, MAC2STR(e->mac));
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* e = (wifi_event_ap_stadisconnected_t*)data;
        ESP_LOGW(TAG, "sta disconnected, mac=" MACSTR ", releasing manual lock", MAC2STR(e->mac));
        // 浏览器关闭时未松手,manual_lock 会卡住让自主模式无法接管,顺手清掉
        atomic_store(&motor_manual_lock, false);
        motor_set(0, 0);
    }
}

static void configure_dhcps_captive(esp_netif_t* ap_netif) {
    esp_netif_dhcps_stop(ap_netif);

    // 把 AP 自身声明为客户端主 DNS,captive_dns 才能截获域名查询
    esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
    IP4_ADDR(&dns.ip.u_addr.ip4, 192, 168, 4, 1);
    esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);

    uint8_t offer_dns = 1;
    esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));
    esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, s_portal_uri,
                           sizeof(s_portal_uri) - 1);

    esp_netif_dhcps_start(ap_netif);
}

void wifi_ap_start(void) {
    esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();
    configure_dhcps_captive(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap =
            {
                .ssid_len       = strlen(CONFIG_ESP_WIFI_SSID),
                .max_connection = CONFIG_ESP_MAX_STA_CONN,
                .authmode       = WIFI_AUTH_WPA2_PSK,
            },
    };
    strlcpy((char*)wifi_config.ap.ssid, CONFIG_ESP_WIFI_SSID, sizeof(wifi_config.ap.ssid));
    strlcpy((char*)wifi_config.ap.password, CONFIG_ESP_WIFI_PASSWORD, sizeof(wifi_config.ap.password));
    if (strlen(CONFIG_ESP_WIFI_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "softap up: ssid='%s'", CONFIG_ESP_WIFI_SSID);
}
