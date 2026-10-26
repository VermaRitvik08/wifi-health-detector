#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <time.h>
#include <sys/time.h>
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#define PROV_POP        "abcd1234"   /* proof-of-possession: the app must know this */
#define PROV_QR_VERSION "v1"

#define MAXIMUM_RETRY  5

/* Event group to track connection status */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi_init";
static int s_retry_num = 0;

void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    
    esp_netif_sntp_init(&config);

    int retry = 0;
    const int retry_count = 10;
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(2000)) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
}

const char *print_local_time(void)
{
    time_t now;
    struct tm timeinfo;
    static char strftime_buf[64];

    time(&now);
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();

    localtime_r(&now, &timeinfo);

    strftime(strftime_buf, sizeof(strftime_buf), "%m/%d/%Y %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
    return strftime_buf;
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
        case WIFI_PROV_CRED_RECV:
            ESP_LOGI(TAG, "Received SSID: %s",
                     (const char *)((wifi_sta_config_t *)event_data)->ssid);
            break;
        case WIFI_PROV_CRED_FAIL:
            ESP_LOGE(TAG, "Provisioning failed (wrong password or AP not found)");
            break;
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            break;
        case WIFI_PROV_END:
            wifi_prov_mgr_deinit();      
            break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Starts Wi-Fi. If credentials are already saved in NVS, connects as a station
 * and returns false. Otherwise starts softAP provisioning as "PROV_XXXXXX"
 * (from the MAC), writes the QR payload the ESP SoftAP Prov app expects into
 * qr_payload and returns true.
 * Call wifi_wait_connected() afterwards in both cases. */
bool wifi_start(char *qr_payload, size_t len)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();          

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_prov_mgr_config_t prov_cfg = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_cfg));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (provisioned) {
        ESP_LOGI(TAG, "Already provisioned, connecting");
        wifi_prov_mgr_deinit();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        return false;
    }

    uint8_t mac[6];
    char service_name[12];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(service_name, sizeof(service_name), "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);

    /* Security 1 + PoP; NULL service key = open softAP (PoP still protects the exchange) */
    ESP_LOGI(TAG, "Starting provisioning as %s", service_name);
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, PROV_POP,
                                                     service_name, NULL));

    snprintf(qr_payload, len,
             "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"softap\"}",
             PROV_QR_VERSION, service_name, PROV_POP);
    ESP_LOGI(TAG, "QR payload: %s", qr_payload);
    return true;
}

bool wifi_wait_connected(void)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

int wifi_rssi(void){
    wifi_ap_record_t ap_info;
    int val = 0;
    if(esp_wifi_sta_get_ap_info(&ap_info)==ESP_OK){
        ESP_LOGI("WIFI", "Connected AP RSSI: %d dBm", ap_info.rssi);
        val = ap_info.rssi;
    } else {
        ESP_LOGE("WIFI", "Failed to get AP info. Is the station connected?");
        return -1;
    }
    return val;
}

const char *wifi_ssid(void){
    wifi_ap_record_t ap_info;
    static char val[33];
    if(esp_wifi_sta_get_ap_info(&ap_info)==ESP_OK){
        strlcpy(val, (char *)ap_info.ssid, sizeof(val));
    } else {
        ESP_LOGE("WIFI", "Failed to get AP info. Is the station connected?");
        val[0] = '\0';
    }
    return val;
}