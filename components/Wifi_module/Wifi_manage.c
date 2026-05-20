#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "Http_ota.h"
#include "ctrl_protocol.h"

#define DEFAULT_WIFI_SSID "ZMJD"
#define DEFAULT_WIFI_PASSWORD "ZM888888"
#define DEFAULT_SERVER_IP "192.168.1.125"
#define DEFAULT_SERVER_PORT 9000
#define DEFAULT_AP_SSID "ESP-WASH"
#define DEFAULT_AP_PASSWORD "12345678"
#define DEFAULT_AP_CHANNEL 1
#define DEFAULT_AP_MAX_CONN 4
#define DEFAULT_LOCAL_TCP_PORT 9000
#define WIFI_TRY_CONNECT_TIMEOUT_MS 30000

#define DEVICE_NVS_PART "device_nvs"
#define DEVICE_NVS_NS "device"
#define DEVICE_SN_KEY "sn"
#define DEVICE_WIFI_SSID_KEY "wifi_ssid"
#define DEVICE_WIFI_PASSWORD_KEY "wifi_pwd"
#define DEVICE_SERVER_IP_KEY "server_ip"
#define DEVICE_SERVER_PORT_KEY "server_port"

#define DEVICE_SN_MAX_LEN 32
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64
#define SERVER_IP_MAX_LEN 64

#define WIFI_CONNECTED_BIT BIT0
#define QUEUE_ITEM_SIZE 256

typedef struct
{
    uint8_t buf[QUEUE_ITEM_SIZE];
    size_t len;
} wifi_data_t;

// Runtime network configuration.
// Defaults come from compile-time macros, then can be overridden from NVS.
// CFG:* commands update runtime configuration.
// SYS:* commands control network operating mode.
typedef struct
{
    char wifi_ssid[WIFI_SSID_MAX_LEN];
    char wifi_password[WIFI_PASSWORD_MAX_LEN];
    char server_ip[SERVER_IP_MAX_LEN];
    uint16_t server_port;
} device_runtime_config_t;

typedef enum
{
    WIFI_RUN_MODE_AP_CONFIG = 0,
    WIFI_RUN_MODE_STA_WORK = 1,
} wifi_run_mode_t;

static const char *TAG = "WIFI_TCP";

static EventGroupHandle_t wifi_event_group = NULL;
static QueueHandle_t wifi_tx_queue = NULL;
static QueueHandle_t wifi_rx_queue = NULL;

static bool s_wifi_stack_initialized = false;
static bool s_wifi_started = false;
static bool s_wifi_ota_started_by_me = false;
static TaskHandle_t s_wifi_ota_ota_task_handle = NULL;
static TaskHandle_t s_local_tcp_server_task_handle = NULL;
static volatile wifi_run_mode_t s_run_mode = WIFI_RUN_MODE_AP_CONFIG;
static volatile bool s_mode_switch_requested = false;

static volatile bool tcp_connected = false;
static volatile bool sn_updated = false;
static volatile bool s_pending_wifi_config_change = false;
static volatile bool s_pending_server_config_change = false;
static volatile bool s_reconnect_requested = false;
static volatile bool s_wifi_reconnect_requested = false;
static volatile bool s_sta_connected = false;
static volatile bool s_wifi_trial_active = false;

static int tcp_sock = -1;
static int s_local_listen_sock = -1;
static int s_local_client_sock = -1;
static int64_t last_recv_tick = 0;
static int64_t s_wifi_trial_deadline_ms = 0;
static char device_sn[DEVICE_SN_MAX_LEN];
static device_runtime_config_t s_runtime_config;
static device_runtime_config_t s_last_good_config;
static device_runtime_config_t s_pre_apply_config;
static bool s_pre_apply_config_valid = false;

static esp_err_t save_sn_to_nvs(const char *sn);
static esp_err_t load_sn_from_nvs(char *sn, size_t len);
static esp_err_t load_device_config_from_nvs(void);
static esp_err_t save_device_config_to_nvs(void);
static void load_default_device_config(void);
static void init_nvs_safe(void);
static void ensure_wifi_stack_initialized(void);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void apply_wifi_sta_config(void);
static void apply_wifi_ap_config(void);
static void switch_to_ap_config_mode(void);
static void switch_to_sta_work_mode(void);
static void stop_local_tcp_server(void);
static bool handle_config_command(const char *input, char *output, int maxlen);
static bool handle_system_command(const char *input, char *output, int maxlen);
static void request_runtime_reconnect(bool wifi_changed, bool server_changed);
static void maybe_rollback_wifi_config(void);
static void local_tcp_server_task(void *param);
static void process_local_tcp_command(int client_sock, char *line_buf, int *line_len);

/* Shared Wi-Fi logging hook. */
void mywifi_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/* Trim trailing whitespace and CRLF from a line. */
static void trim_line(char *text)
{
    size_t len = strlen(text);
    while (len > 0)
    {
        char c = text[len - 1];
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t')
        {
            break;
        }
        text[--len] = '\0';
    }
}

/* Copy a config string only if it fits and is non-empty. */
static bool copy_config_value(char *dest, size_t dest_size, const char *value)
{
    size_t len = strlen(value);
    if (len == 0 || len >= dest_size)
    {
        return false;
    }

    memcpy(dest, value, len + 1);
    return true;
}

/* Load the built-in default network configuration. */
static void load_default_device_config(void)
{
    memset(&s_runtime_config, 0, sizeof(s_runtime_config));
    copy_config_value(s_runtime_config.wifi_ssid, sizeof(s_runtime_config.wifi_ssid), DEFAULT_WIFI_SSID);
    copy_config_value(
        s_runtime_config.wifi_password,
        sizeof(s_runtime_config.wifi_password),
        DEFAULT_WIFI_PASSWORD);
    copy_config_value(s_runtime_config.server_ip, sizeof(s_runtime_config.server_ip), DEFAULT_SERVER_IP);
    s_runtime_config.server_port = DEFAULT_SERVER_PORT;
}

/* Read a string from NVS; keep the current value on failure. */
static void load_nvs_str_or_default(
    nvs_handle_t nvs,
    const char *key,
    char *buffer,
    size_t buffer_len)
{
    size_t required = buffer_len;
    if (nvs_get_str(nvs, key, buffer, &required) != ESP_OK)
    {
        return;
    }
    buffer[buffer_len - 1] = '\0';
}

/* Restore device network config from device_nvs. */
static esp_err_t load_device_config_from_nvs(void)
{
    nvs_handle_t nvs;
    uint16_t saved_port = 0;
    esp_err_t err;
    bool should_persist_defaults = false;

    load_default_device_config();

    err = nvs_open_from_partition(DEVICE_NVS_PART, DEVICE_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        memcpy(&s_last_good_config, &s_runtime_config, sizeof(s_last_good_config));
        ESP_LOGW(TAG, "Device config NVS open failed, use defaults: %s", esp_err_to_name(err));
        return err;
    }

    load_nvs_str_or_default(
        nvs,
        DEVICE_WIFI_SSID_KEY,
        s_runtime_config.wifi_ssid,
        sizeof(s_runtime_config.wifi_ssid));
    load_nvs_str_or_default(
        nvs,
        DEVICE_WIFI_PASSWORD_KEY,
        s_runtime_config.wifi_password,
        sizeof(s_runtime_config.wifi_password));
    load_nvs_str_or_default(
        nvs,
        DEVICE_SERVER_IP_KEY,
        s_runtime_config.server_ip,
        sizeof(s_runtime_config.server_ip));

    if (nvs_get_u16(nvs, DEVICE_SERVER_PORT_KEY, &saved_port) == ESP_OK && saved_port > 0)
    {
        s_runtime_config.server_port = saved_port;
    }

    nvs_close(nvs);

    if (s_runtime_config.wifi_password[0] == '\0')
    {
        copy_config_value(
            s_runtime_config.wifi_password,
            sizeof(s_runtime_config.wifi_password),
            DEFAULT_WIFI_PASSWORD);
        should_persist_defaults = true;
        ESP_LOGW(TAG, "Stored WiFi password is empty, restored default password");
    }

    if (should_persist_defaults)
    {
        esp_err_t save_err = save_device_config_to_nvs();
        if (save_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to persist restored defaults: %s", esp_err_to_name(save_err));
        }
    }

    ESP_LOGI(
        TAG,
        "Runtime config loaded: ssid=%s server=%s:%u",
        s_runtime_config.wifi_ssid,
        s_runtime_config.server_ip,
        s_runtime_config.server_port);
    memcpy(&s_last_good_config, &s_runtime_config, sizeof(s_last_good_config));
    return ESP_OK;
}

/* Persist the current runtime config into device_nvs. */
static esp_err_t save_device_config_to_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open_from_partition(DEVICE_NVS_PART, DEVICE_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_str(nvs, DEVICE_WIFI_SSID_KEY, s_runtime_config.wifi_ssid);
    if (err == ESP_OK)
        err = nvs_set_str(nvs, DEVICE_WIFI_PASSWORD_KEY, s_runtime_config.wifi_password);
    if (err == ESP_OK)
        err = nvs_set_str(nvs, DEVICE_SERVER_IP_KEY, s_runtime_config.server_ip);
    if (err == ESP_OK)
        err = nvs_set_u16(nvs, DEVICE_SERVER_PORT_KEY, s_runtime_config.server_port);
    if (err == ESP_OK)
        err = nvs_commit(nvs);

    nvs_close(nvs);
    return err;
}

/* Initialize default NVS and the custom device_nvs partition safely. */
static void init_nvs_safe(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    err = nvs_flash_init_partition(DEVICE_NVS_PART);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase_partition(DEVICE_NVS_PART));
        ESP_ERROR_CHECK(nvs_flash_init_partition(DEVICE_NVS_PART));
    }

    ESP_LOGI(TAG, "NVS init OK");
}

/* One-time initialization of netif, event loop, and Wi-Fi stack. */
static void ensure_wifi_stack_initialized(void)
{
    if (s_wifi_stack_initialized)
    {
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    s_wifi_stack_initialized = true;
}

/* Configure the device SoftAP used for phone-side setup/debugging. */
static void apply_wifi_ap_config(void)
{
    wifi_config_t ap_config = {0};

    copy_config_value((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), DEFAULT_AP_SSID);
    copy_config_value((char *)ap_config.ap.password, sizeof(ap_config.ap.password), DEFAULT_AP_PASSWORD);
    ap_config.ap.ssid_len = strlen(DEFAULT_AP_SSID);
    ap_config.ap.channel = DEFAULT_AP_CHANNEL;
    ap_config.ap.max_connection = DEFAULT_AP_MAX_CONN;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    if (strlen(DEFAULT_AP_PASSWORD) == 0)
    {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_LOGI(TAG, "SoftAP ready: ssid=%s", DEFAULT_AP_SSID);
}

static void stop_local_tcp_server(void)
{
    if (s_local_client_sock >= 0)
    {
        shutdown(s_local_client_sock, SHUT_RDWR);
        close(s_local_client_sock);
        s_local_client_sock = -1;
    }

    if (s_local_listen_sock >= 0)
    {
        shutdown(s_local_listen_sock, SHUT_RDWR);
        close(s_local_listen_sock);
        s_local_listen_sock = -1;
    }

    if (s_local_tcp_server_task_handle != NULL)
    {
        vTaskDelete(s_local_tcp_server_task_handle);
        s_local_tcp_server_task_handle = NULL;
    }
}

static void switch_to_ap_config_mode(void)
{
    s_run_mode = WIFI_RUN_MODE_AP_CONFIG;
    s_mode_switch_requested = false;
    s_wifi_reconnect_requested = false;
    s_reconnect_requested = true;
    s_wifi_trial_active = false;
    s_sta_connected = false;
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

    stop_local_tcp_server();
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    apply_wifi_ap_config();

    if (!s_wifi_started)
    {
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
    }

    if (s_local_tcp_server_task_handle == NULL)
    {
        xTaskCreate(local_tcp_server_task, "tcp_server", 4096, NULL, 5, &s_local_tcp_server_task_handle);
    }

    ESP_LOGI(TAG, "Switched to AP config mode");
}

static void switch_to_sta_work_mode(void)
{
    wifi_config_t wifi_config = {0};

    s_run_mode = WIFI_RUN_MODE_STA_WORK;
    s_mode_switch_requested = false;
    s_reconnect_requested = true;
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

    copy_config_value((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), s_runtime_config.wifi_ssid);
    if (s_runtime_config.wifi_password[0] != '\0')
    {
        copy_config_value((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), s_runtime_config.wifi_password);
    }

    stop_local_tcp_server();
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    if (!s_wifi_started)
    {
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
    }
    else
    {
        esp_wifi_connect();
    }

    ESP_LOGI(TAG, "Switched to STA work mode: ssid=%s", s_runtime_config.wifi_ssid);
}

/* Wi-Fi event handler for STA connect/disconnect/IP events. */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        if (s_run_mode == WIFI_RUN_MODE_STA_WORK)
        {
            esp_wifi_connect();
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START)
    {
        if (s_run_mode == WIFI_RUN_MODE_AP_CONFIG && s_local_tcp_server_task_handle == NULL)
        {
            xTaskCreate(local_tcp_server_task, "tcp_server", 4096, NULL, 5, &s_local_tcp_server_task_handle);
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        s_sta_connected = false;
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_run_mode == WIFI_RUN_MODE_STA_WORK)
        {
            ESP_LOGW(TAG, "WiFi disconnected, retry...");
            esp_wifi_connect();
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        s_sta_connected = true;
        if (s_wifi_trial_active)
        {
            s_wifi_trial_active = false;
            s_pre_apply_config_valid = false;
            memcpy(&s_last_good_config, &s_runtime_config, sizeof(s_last_good_config));
            ESP_LOGI(TAG, "WiFi config verified and committed");
        }
        ESP_LOGI(TAG, "WiFi connected");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Apply the current STA config and reconnect when settings change. */
static void apply_wifi_sta_config(void)
{
    switch_to_sta_work_mode();
}

/* Initialize the Wi-Fi RX/TX queues. */
void wifi_module_queue_init(void)
{
    if (wifi_tx_queue == NULL)
    {
        wifi_tx_queue = xQueueCreate(128, sizeof(wifi_data_t));
    }
    if (wifi_rx_queue == NULL)
    {
        wifi_rx_queue = xQueueCreate(128, sizeof(wifi_data_t));
    }

    if (wifi_tx_queue == NULL || wifi_rx_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi queues");
        return;
    }

    ESP_LOGI(TAG, "WiFi queues created successfully");
}

/* Start in AP config mode so the phone can configure the device locally. */
static void wifi_init_sta(void)
{
    if (wifi_event_group == NULL)
    {
        wifi_event_group = xEventGroupCreate();
    }

    ensure_wifi_stack_initialized();
    switch_to_ap_config_mode();
    ESP_LOGI(TAG, "wifi_init finished");
}

/* Enter OTA mode and start the HTTP OTA task after Wi-Fi is ready. */
void wifi_ota_mode_start(const char *default_url)
{
    ESP_LOGI(TAG, "Entering OTA mode");

    if (!s_wifi_started)
    {
        wifi_init_sta();
        s_wifi_ota_started_by_me = true;
    }

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(30000));
    if ((bits & WIFI_CONNECTED_BIT) == 0)
    {
        ESP_LOGE(TAG, "OTA mode: waiting for IP timed out");
        return;
    }

    http_ota_config_t cfg = {0};
    if (default_url)
    {
        strncpy(cfg.firmware_url, default_url, sizeof(cfg.firmware_url) - 1);
    }
    cfg.task_stack_size = 8192;
    cfg.task_prio = 5;

    s_wifi_ota_ota_task_handle = http_ota_start(&cfg);
    if (s_wifi_ota_ota_task_handle)
    {
        ESP_LOGI(TAG, "OTA task started");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start OTA task");
    }
}

/* Exit OTA mode and release OTA-owned resources. */
void wifi_ota_mode_stop(void)
{
    ESP_LOGI(TAG, "Exiting OTA mode");

    if (s_wifi_ota_ota_task_handle)
    {
        http_ota_stop(s_wifi_ota_ota_task_handle);
        s_wifi_ota_ota_task_handle = NULL;
    }

    if (s_wifi_ota_started_by_me)
    {
        esp_wifi_stop();
        s_wifi_started = false;
        s_wifi_ota_started_by_me = false;
    }
}

/* Mark reconnect requirements after runtime config changes. */
static void request_runtime_reconnect(bool wifi_changed, bool server_changed)
{
    s_run_mode = WIFI_RUN_MODE_STA_WORK;
    s_mode_switch_requested = true;

    if (wifi_changed)
    {
        s_wifi_reconnect_requested = true;
        s_wifi_trial_active = true;
        s_sta_connected = false;
        s_wifi_trial_deadline_ms = esp_timer_get_time() / 1000 + WIFI_TRY_CONNECT_TIMEOUT_MS;
    }
    if (wifi_changed || server_changed)
    {
        s_reconnect_requested = true;
    }
}

static void maybe_rollback_wifi_config(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (!s_wifi_trial_active || s_sta_connected || now_ms < s_wifi_trial_deadline_ms)
    {
        return;
    }

    s_wifi_trial_active = false;

    if (!s_pre_apply_config_valid)
    {
        ESP_LOGE(TAG, "WiFi trial failed, but no previous config snapshot is available");
        return;
    }

    memcpy(&s_runtime_config, &s_pre_apply_config, sizeof(s_runtime_config));
    memcpy(&s_last_good_config, &s_pre_apply_config, sizeof(s_last_good_config));
    s_pre_apply_config_valid = false;
    s_pending_wifi_config_change = false;
    s_pending_server_config_change = false;

    if (save_device_config_to_nvs() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to persist rolled back WiFi config");
    }

    ESP_LOGW(TAG, "WiFi trial timed out, rolling back to previous config: ssid=%s", s_runtime_config.wifi_ssid);
    s_run_mode = WIFI_RUN_MODE_AP_CONFIG;
    s_mode_switch_requested = true;
    s_wifi_reconnect_requested = false;
    s_reconnect_requested = true;
}

static bool handle_system_command(const char *input, char *output, int maxlen)
{
    if (strncmp(input, "SYS:", 4) != 0)
    {
        return false;
    }

    if (strcmp(input, "SYS:MODE=STA") == 0)
    {
        esp_err_t err = save_device_config_to_nvs();
        if (err != ESP_OK)
        {
            snprintf(output, maxlen, "SYS:ERR,MODE\r\n");
            return true;
        }

        request_runtime_reconnect(s_pending_wifi_config_change, s_pending_server_config_change);
        s_pending_wifi_config_change = false;
        s_pending_server_config_change = false;
        snprintf(output, maxlen, "SYS:OK,MODE=STA\r\n");
        return true;
    }

    if (strcmp(input, "SYS:MODE=AP") == 0)
    {
        s_wifi_trial_active = false;
        s_sta_connected = false;
        s_pre_apply_config_valid = false;
        s_run_mode = WIFI_RUN_MODE_AP_CONFIG;
        s_mode_switch_requested = true;
        s_wifi_reconnect_requested = false;
        s_reconnect_requested = true;
        snprintf(output, maxlen, "SYS:OK,MODE=AP\r\n");
        return true;
    }

    if (strcmp(input, "SYS:STATUS") == 0)
    {
        snprintf(
            output,
            maxlen,
            "SYS:STATUS,MODE=%s,STA=%s,TCP=%s,TRIAL=%s\r\n",
            s_run_mode == WIFI_RUN_MODE_STA_WORK ? "STA" : "AP",
            s_sta_connected ? "CONNECTED" : "DISCONNECTED",
            tcp_connected ? "CONNECTED" : "DISCONNECTED",
            s_wifi_trial_active ? "ACTIVE" : "IDLE");
        return true;
    }

    snprintf(output, maxlen, "SYS:ERR,UNKNOWN\r\n");
    return true;
}

/* Handle CFG:* commands for Wi-Fi/server configuration. */
static bool handle_config_command(const char *input, char *output, int maxlen)
{
    const char *value = NULL;

    if (strncmp(input, "CFG:", 4) != 0)
    {
        return false;
    }

    if (strcmp(input, "CFG:GET") == 0)
    {
        snprintf(
            output,
            maxlen,
            "CFG:GET,SSID=%s,WIFI_PASSWORD=%s,SERVER_IP=%s,SERVER_PORT=%u\r\n",
            s_runtime_config.wifi_ssid,
            s_runtime_config.wifi_password,
            s_runtime_config.server_ip,
            s_runtime_config.server_port);
        return true;
    }

    value = strstr(input, "CFG:WIFI_SSID=");
    if (value == input)
    {
        if (!s_pending_wifi_config_change)
        {
            memcpy(&s_pre_apply_config, &s_runtime_config, sizeof(s_pre_apply_config));
            s_pre_apply_config_valid = true;
        }
        value += strlen("CFG:WIFI_SSID=");
        if (!copy_config_value(s_runtime_config.wifi_ssid, sizeof(s_runtime_config.wifi_ssid), value))
        {
            snprintf(output, maxlen, "CFG:ERR,WIFI_SSID\r\n");
            return true;
        }
        s_pending_wifi_config_change = true;
        snprintf(output, maxlen, "CFG:OK,WIFI_SSID\r\n");
        return true;
    }

    value = strstr(input, "CFG:WIFI_PASSWORD=");
    if (value == input)
    {
        if (!s_pending_wifi_config_change)
        {
            memcpy(&s_pre_apply_config, &s_runtime_config, sizeof(s_pre_apply_config));
            s_pre_apply_config_valid = true;
        }
        value += strlen("CFG:WIFI_PASSWORD=");
        if (strlen(value) >= sizeof(s_runtime_config.wifi_password))
        {
            snprintf(output, maxlen, "CFG:ERR,WIFI_PASSWORD\r\n");
            return true;
        }
        strncpy(s_runtime_config.wifi_password, value, sizeof(s_runtime_config.wifi_password) - 1);
        s_runtime_config.wifi_password[sizeof(s_runtime_config.wifi_password) - 1] = '\0';
        s_pending_wifi_config_change = true;
        snprintf(output, maxlen, "CFG:OK,WIFI_PASSWORD\r\n");
        return true;
    }

    value = strstr(input, "CFG:SERVER_IP=");
    if (value == input)
    {
        value += strlen("CFG:SERVER_IP=");
        if (!copy_config_value(s_runtime_config.server_ip, sizeof(s_runtime_config.server_ip), value))
        {
            snprintf(output, maxlen, "CFG:ERR,SERVER_IP\r\n");
            return true;
        }
        s_pending_server_config_change = true;
        snprintf(output, maxlen, "CFG:OK,SERVER_IP\r\n");
        return true;
    }

    value = strstr(input, "CFG:SERVER_PORT=");
    if (value == input)
    {
        char *end = NULL;
        long port = 0;

        value += strlen("CFG:SERVER_PORT=");
        port = strtol(value, &end, 10);
        if (end == value || *end != '\0' || port <= 0 || port > 65535)
        {
            snprintf(output, maxlen, "CFG:ERR,SERVER_PORT\r\n");
            return true;
        }

        s_runtime_config.server_port = (uint16_t)port;
        s_pending_server_config_change = true;
        snprintf(output, maxlen, "CFG:OK,SERVER_PORT\r\n");
        return true;
    }

    if (strcmp(input, "CFG:SAVE") == 0)
    {
        esp_err_t err = save_device_config_to_nvs();
        snprintf(
            output,
            maxlen,
            err == ESP_OK ? "CFG:OK,SAVED\r\n" : "CFG:ERR,SAVE\r\n");
        return true;
    }

    if (strcmp(input, "CFG:APPLY") == 0)
    {
        bool handled = handle_system_command("SYS:MODE=STA", output, maxlen);
        if (handled && strncmp(output, "SYS:OK,MODE=STA", 15) == 0)
        {
            snprintf(output, maxlen, "CFG:OK,APPLY\r\n");
        }
        else if (handled)
        {
            snprintf(output, maxlen, "CFG:ERR,APPLY\r\n");
        }
        return true;
    }

    if (strcmp(input, "CFG:CANCEL") == 0)
    {
        handle_system_command("SYS:MODE=AP", output, maxlen);
        snprintf(output, maxlen, "CFG:OK,CANCEL\r\n");
        return true;
    }

    snprintf(output, maxlen, "CFG:ERR,UNKNOWN\r\n");
    return true;
}

/* Public config-command entry used by BLE and other transports. */
bool wifi_module_handle_config_command(const char *input, char *output, int maxlen)
{
    if (handle_config_command(input, output, maxlen))
    {
        return true;
    }

    return handle_system_command(input, output, maxlen);
}

static void send_response_with_prefix(int sock, const char *prefix, const char *response)
{
    char tx_buf[QUEUE_ITEM_SIZE] = {0};

    if (response == NULL || response[0] == '\0')
    {
        return;
    }

    if (prefix != NULL && prefix[0] != '\0')
    {
        snprintf(tx_buf, sizeof(tx_buf), "%s%.*s", prefix, (int)sizeof(tx_buf) - 8, response);
    }
    else
    {
        snprintf(tx_buf, sizeof(tx_buf), "%s", response);
    }

    size_t len = strlen(tx_buf);
    if (len == 0)
    {
        return;
    }

    if (tx_buf[len - 1] != '\n' && len < sizeof(tx_buf) - 1)
    {
        tx_buf[len++] = '\n';
        tx_buf[len] = '\0';
    }

    send(sock, tx_buf, len, 0);
}

static void process_local_tcp_command(int client_sock, char *line_buf, int *line_len)
{
    char response[QUEUE_ITEM_SIZE] = {0};

    if (*line_len <= 0)
    {
        return;
    }

    line_buf[*line_len] = '\0';
    trim_line(line_buf);
    *line_len = 0;

    if (line_buf[0] == '\0')
    {
        return;
    }

    ESP_LOGI(TAG, "LOCAL TCP RX: %s", line_buf);
    ctrl_protocol(line_buf, response, sizeof(response));
    send_response_with_prefix(client_sock, NULL, response);
}

/* Ignore transport-layer server messages that are not business commands. */
static bool should_ignore_server_line(const char *line)
{
    return strcmp(line, "REG,OK") == 0 || strcmp(line, "PING") == 0 || strcmp(line, "PONG") == 0;
}

/* TCP client task used for upstream server communication. */
void tcp_client_task(void *param)
{
    char rx_buf[256];
    char line_buf[256];
    int line_len = 0;

    while (1)
    {
        maybe_rollback_wifi_config();

        if (s_mode_switch_requested)
        {
            if (s_run_mode == WIFI_RUN_MODE_AP_CONFIG)
            {
                switch_to_ap_config_mode();
            }
            else
            {
                switch_to_sta_work_mode();
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        if (s_run_mode != WIFI_RUN_MODE_STA_WORK)
        {
            if (tcp_connected && tcp_sock >= 0)
            {
                shutdown(tcp_sock, SHUT_RDWR);
                close(tcp_sock);
                tcp_sock = -1;
                tcp_connected = false;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(1000));
        if ((bits & WIFI_CONNECTED_BIT) == 0)
        {
            continue;
        }

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "socket create failed, errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(s_runtime_config.server_port),
            .sin_addr.s_addr = inet_addr(s_runtime_config.server_ip),
        };

        ESP_LOGI(TAG, "Connecting to %s:%u", s_runtime_config.server_ip, s_runtime_config.server_port);
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
        {
            ESP_LOGE(TAG, "TCP connect failed, errno=%d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        tcp_sock = sock;
        tcp_connected = true;
        s_reconnect_requested = false;
        line_len = 0;
        last_recv_tick = esp_timer_get_time() / 1000;

        ESP_LOGI(TAG, "TCP connected");

        char reg[64];
        snprintf(reg, sizeof(reg), "REG|%s|1.0.0\n", device_sn);
        send(sock, reg, strlen(reg), 0);

        while (1)
        {
            fd_set rfds;
            struct timeval tv = {.tv_sec = 1, .tv_usec = 0};

            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);

            int ret = select(sock + 1, &rfds, NULL, NULL, &tv);
            if (ret < 0)
            {
                ESP_LOGE(TAG, "select failed, errno=%d", errno);
                break;
            }

            if (ret > 0 && FD_ISSET(sock, &rfds))
            {
                int len = recv(sock, rx_buf, sizeof(rx_buf), 0);
                if (len <= 0)
                {
                    break;
                }

                last_recv_tick = esp_timer_get_time() / 1000;

                for (int i = 0; i < len; i++)
                {
                    char c = rx_buf[i];
                    if (c == '\r')
                    {
                        continue;
                    }

                    if (c == '\n')
                    {
                        line_buf[line_len] = '\0';
                        trim_line(line_buf);

                        if (line_buf[0] != '\0' && !should_ignore_server_line(line_buf))
                        {
                            char *payload = line_buf;
                            if (strncmp(payload, "CMD|", 4) == 0)
                            {
                                payload += 4;
                            }

                            wifi_data_t pkt = {0};
                            strncpy((char *)pkt.buf, payload, QUEUE_ITEM_SIZE - 1);
                            pkt.buf[QUEUE_ITEM_SIZE - 1] = '\0';
                            pkt.len = strlen((char *)pkt.buf);
                            xQueueSend(wifi_rx_queue, &pkt, 0);
                        }

                        line_len = 0;
                    }
                    else if (line_len < (int)sizeof(line_buf) - 1)
                    {
                        line_buf[line_len++] = c;
                    }
                }
            }

            wifi_data_t tx = {0};
            if (xQueueReceive(wifi_tx_queue, &tx, 0) == pdTRUE)
            {
                send(sock, tx.buf, tx.len, 0);
            }

            if (s_reconnect_requested)
            {
                ESP_LOGW(TAG, "Reconnect requested, closing current TCP session");
                break;
            }

            if (s_run_mode != WIFI_RUN_MODE_STA_WORK || s_mode_switch_requested)
            {
                break;
            }
        }

        ESP_LOGW(TAG, "TCP disconnected");
        tcp_connected = false;
        tcp_sock = -1;
        shutdown(sock, SHUT_RDWR);
        close(sock);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void local_tcp_server_task(void *param)
{
    struct sockaddr_in server_addr = {0};
    struct sockaddr_in client_addr = {0};
    socklen_t client_addr_len = sizeof(client_addr);
    int listen_sock = -1;
    int client_sock = -1;
    char rx_buf[QUEUE_ITEM_SIZE] = {0};
    char line_buf[QUEUE_ITEM_SIZE] = {0};
    int line_len = 0;

    vTaskDelay(pdMS_TO_TICKS(500));

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    s_local_listen_sock = listen_sock;
    if (listen_sock < 0)
    {
        ESP_LOGE(TAG, "Local TCP server socket create failed: errno=%d", errno);
        s_local_listen_sock = -1;
        s_local_tcp_server_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEFAULT_LOCAL_TCP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        ESP_LOGE(TAG, "Local TCP server bind failed: errno=%d", errno);
        close(listen_sock);
        s_local_listen_sock = -1;
        s_local_tcp_server_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 2) != 0)
    {
        ESP_LOGE(TAG, "Local TCP server listen failed: errno=%d", errno);
        close(listen_sock);
        s_local_listen_sock = -1;
        s_local_tcp_server_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Local TCP server listening on 192.168.4.1:%d", DEFAULT_LOCAL_TCP_PORT);

    while (1)
    {
        client_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        s_local_client_sock = client_sock;
        if (client_sock < 0)
        {
            if (s_run_mode != WIFI_RUN_MODE_AP_CONFIG)
            {
                break;
            }
            ESP_LOGE(TAG, "Local TCP server accept failed: errno=%d", errno);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        ESP_LOGI(TAG, "Local TCP client connected: %s:%u", client_ip, ntohs(client_addr.sin_port));
        line_len = 0;

        while (1)
        {
            int len = recv(client_sock, rx_buf, sizeof(rx_buf), 0);
            bool saw_newline = false;
            if (len <= 0)
            {
                process_local_tcp_command(client_sock, line_buf, &line_len);
                ESP_LOGW(TAG, "Local TCP client disconnected");
                break;
            }

            for (int i = 0; i < len; i++)
            {
                char c = rx_buf[i];
                if (c == '\r')
                {
                    continue;
                }

                if (c == '\n')
                {
                    saw_newline = true;
                    process_local_tcp_command(client_sock, line_buf, &line_len);
                    continue;
                }

                if (line_len < (int)sizeof(line_buf) - 1)
                {
                    line_buf[line_len++] = c;
                }
            }

            if (!saw_newline && line_len > 0)
            {
                process_local_tcp_command(client_sock, line_buf, &line_len);
            }
        }

        shutdown(client_sock, SHUT_RDWR);
        close(client_sock);
        s_local_client_sock = -1;
    }

    if (listen_sock >= 0)
    {
        close(listen_sock);
    }
    s_local_client_sock = -1;
    s_local_listen_sock = -1;
    s_local_tcp_server_task_handle = NULL;
    vTaskDelete(NULL);
}

/* Dispatch TCP-client payloads into ctrl_protocol(). */
void wifi_protocol_task(void *param)
{
    wifi_data_t rx;
    char response[256];

    while (1)
    {
        if (xQueueReceive(wifi_rx_queue, &rx, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        if (rx.len >= QUEUE_ITEM_SIZE)
        {
            rx.len = QUEUE_ITEM_SIZE - 1;
        }
        rx.buf[rx.len] = '\0';
        trim_line((char *)rx.buf);

        ESP_LOGI(TAG, "WIFI RX: %s", rx.buf);
        memset(response, 0, sizeof(response));

        ctrl_protocol((char *)rx.buf, response, sizeof(response));
        if (response[0] == '\0')
        {
            continue;
        }

        wifi_data_t tx = {0};
        snprintf((char *)tx.buf, QUEUE_ITEM_SIZE, "ACK|%.*s", QUEUE_ITEM_SIZE - 5, response);

        size_t len = strlen((char *)tx.buf);
        if (len == 0 || tx.buf[len - 1] != '\n')
        {
            if (len < QUEUE_ITEM_SIZE - 1)
            {
                tx.buf[len++] = '\n';
                tx.buf[len] = '\0';
            }
        }

        tx.len = len;
        xQueueSend(wifi_tx_queue, &tx, 0);
    }
}

/* Load device SN from device_nvs. */
static esp_err_t load_sn_from_nvs(char *sn, size_t len)
{
    nvs_handle_t nvs;
    size_t required_len = len;
    esp_err_t err = nvs_open_from_partition(DEVICE_NVS_PART, DEVICE_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_get_str(nvs, DEVICE_SN_KEY, sn, &required_len);
    nvs_close(nvs);
    return err;
}

/* Save device SN into device_nvs. */
static esp_err_t save_sn_to_nvs(const char *sn)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open_from_partition(DEVICE_NVS_PART, DEVICE_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_str(nvs, DEVICE_SN_KEY, sn);
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

/* Generate a default SN from the Wi-Fi STA MAC address. */
static void generate_sn(char *sn, size_t len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(sn, len, "SN_%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Initialize device SN from NVS, or generate and save a new one. */
static void init_device_sn(void)
{
    memset(device_sn, 0, sizeof(device_sn));
    if (load_sn_from_nvs(device_sn, sizeof(device_sn)) == ESP_OK)
    {
        ESP_LOGI(TAG, "Device SN loaded: %s", device_sn);
        return;
    }

    ESP_LOGW(TAG, "No SN found, generating new one");
    generate_sn(device_sn, sizeof(device_sn));
    if (save_sn_to_nvs(device_sn) == ESP_OK)
    {
        ESP_LOGI(TAG, "New SN saved: %s", device_sn);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to save SN");
    }
}

/* Start Wi-Fi, protocol tasks, the upstream TCP client, and the local TCP server. */
void wifi_tcp_start(void)
{
    init_nvs_safe();
    init_device_sn();
    load_device_config_from_nvs();
    wifi_module_queue_init();
    wifi_init_sta();

    xTaskCreate(wifi_protocol_task, "wifi_proto", 4096, NULL, 6, NULL);
    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
}
