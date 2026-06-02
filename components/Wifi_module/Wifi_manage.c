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

/*
 * WiFi 模块说明（Wifi_manage.c）
 *
 * 【两种工作模式】
 *   AP 配网模式：设备开热点 ESP-WASH，手机连上后 TCP 连 192.168.4.1:9000 发 CFG:/SYS: 命令
 *   STA 工作模式：设备连路由器，再作为 TCP 客户端连云端 server_ip:server_port
 *
 * 【协议】与蓝牙相同：发什么命令，ctrl_protocol 原样回什么（无 CMD|/ACK| 包装）
 *
 * 【三个任务】
 *   tcp_client_task    — STA 时连云端，收发 TCP
 *   wifi_protocol_task — 把云端收到的命令交给 ctrl_protocol，应答放入发送队列
 *   local_tcp_server_task — AP 时监听 9000，手机直连配网
 *
 * 【上电逻辑】NVS 里 wifi_prov=1 表示已配网 → 直接 STA；否则先进 AP
 */

/* 默认要连接的路由器 SSID（可被 NVS / CFG:WIFI_SSID 覆盖） */
#define DEFAULT_WIFI_SSID "ZMJD"
/* 默认路由器密码 */
#define DEFAULT_WIFI_PASSWORD "ZM888888"
/* 默认云端 TCP 服务器 IP */
#define DEFAULT_SERVER_IP "192.168.1.125"
/* 默认云端 TCP 端口 */
#define DEFAULT_SERVER_PORT 9000

/* 配网热点名称（固定，与 CFG:WIFI_SSID 无关） */
#define DEFAULT_AP_SSID "ESP-WASH"
/* 配网热点密码 */
#define DEFAULT_AP_PASSWORD "12345678"
/* 配网热点 WiFi 信道 */
#define DEFAULT_AP_CHANNEL 1
/* 配网热点允许同时连接的最大设备数 */
#define DEFAULT_AP_MAX_CONN 4
/* AP 模式下本地 TCP 监听端口（手机连 192.168.4.1:此端口） */
#define DEFAULT_LOCAL_TCP_PORT 9000

/* 试连新 WiFi 的超时时间（毫秒），超时则回滚并退回 AP */
#define WIFI_TRY_CONNECT_TIMEOUT_MS 30000

/* NVS 分区名：存放设备 SN、网络配置等 */
#define DEVICE_NVS_PART "device_nvs"
/* NVS 命名空间 */
#define DEVICE_NVS_NS "device"
/* NVS 键：设备序列号 */
#define DEVICE_SN_KEY "sn"
/* NVS 键：路由器 SSID */
#define DEVICE_WIFI_SSID_KEY "wifi_ssid"
/* NVS 键：路由器密码 */
#define DEVICE_WIFI_PASSWORD_KEY "wifi_pwd"
/* NVS 键：云端服务器 IP */
#define DEVICE_SERVER_IP_KEY "server_ip"
/* NVS 键：云端服务器端口 */
#define DEVICE_SERVER_PORT_KEY "server_port"
/* NVS 键：是否已完成配网（1=已配网，上电直接 STA） */
#define DEVICE_PROVISIONED_KEY "wifi_prov"

/* 设备 SN 字符串最大长度 */
#define DEVICE_SN_MAX_LEN 32
/* 路由器 SSID 最大长度 */
#define WIFI_SSID_MAX_LEN 32
/* 路由器密码最大长度 */
#define WIFI_PASSWORD_MAX_LEN 64
/* 云端 IP 字符串最大长度 */
#define SERVER_IP_MAX_LEN 64

/* 事件组位：STA 已获取 IP */
#define WIFI_CONNECTED_BIT BIT0
/* WiFi 收发队列单条消息最大字节数 */
#define QUEUE_ITEM_SIZE 256

/* 云端 TCP 收发队列里的一帧数据 */
typedef struct
{
    uint8_t buf[QUEUE_ITEM_SIZE]; /* 命令或应答内容 */
    size_t len;                   /* buf 中有效字节数 */
} wifi_data_t;

/* 当前生效的网络配置（内存）；CFG:* 修改，SYS:MODE=STA 时写入 NVS */
typedef struct
{
    char wifi_ssid[WIFI_SSID_MAX_LEN];           /* 要连接的路由器 SSID */
    char wifi_password[WIFI_PASSWORD_MAX_LEN]; /* 要连接的路由器密码 */
    char server_ip[SERVER_IP_MAX_LEN];           /* 云端 TCP 服务器 IP */
    uint16_t server_port;                        /* 云端 TCP 端口 */
} device_runtime_config_t;

/* WiFi 运行模式 */
typedef enum
{
    WIFI_RUN_MODE_AP_CONFIG = 0, /* AP 配网：开热点 + 本地 TCP */
    WIFI_RUN_MODE_STA_WORK = 1,  /* STA 工作：连路由器 + 云端 TCP */
} wifi_run_mode_t;

static const char *TAG = "WIFI_TCP"; /* 日志标签 */

static EventGroupHandle_t wifi_event_group = NULL; /* WiFi 事件组（STA 获 IP 等） */
static QueueHandle_t wifi_tx_queue = NULL;         /* 发往云端的应答队列 */
static QueueHandle_t wifi_rx_queue = NULL;         /* 云端下发的命令队列 */

static bool s_wifi_stack_initialized = false;              /* WiFi 协议栈是否已初始化 */
static bool s_wifi_started = false;                        /* WiFi 驱动是否已 start */
static bool s_wifi_ota_started_by_me = false;              /* OTA 流程是否由本模块启动 WiFi */
static TaskHandle_t s_wifi_ota_ota_task_handle = NULL;     /* HTTP OTA 任务句柄 */
static TaskHandle_t s_local_tcp_server_task_handle = NULL; /* AP 本地 TCP 服务任务句柄 */
static volatile wifi_run_mode_t s_run_mode = WIFI_RUN_MODE_AP_CONFIG; /* 当前运行模式 */
static volatile bool s_mode_switch_requested = false;      /* 是否请求切换 AP/STA */

static volatile bool tcp_connected = false;                /* 与云端 TCP 是否已连接 */
static volatile bool sn_updated = false;                   /* SN 是否被更新（预留） */
static volatile bool s_pending_wifi_config_change = false;   /* CFG 已改 WiFi，待 STA 生效 */
static volatile bool s_pending_server_config_change = false; /* CFG 已改云端地址 */
static volatile bool s_reconnect_requested = false;          /* 需要断开并重连云端 TCP */
static volatile bool s_wifi_reconnect_requested = false;     /* 需要按新配置重连 WiFi */
static volatile bool s_sta_connected = false;              /* 路由器 WiFi 是否已连接 */
static volatile bool s_wifi_trial_active = false;            /* 正在试连新 WiFi（可超时回滚） */
static volatile bool s_local_tcp_server_stop_requested = false; /* 请求停止 AP 本地 TCP 服务 */

static int tcp_sock = -1;            /* 云端 TCP 套接字描述符，-1 表示未连接 */
static int s_local_listen_sock = -1; /* AP 模式 TCP 监听套接字 */
static int s_local_client_sock = -1;  /* AP 模式当前连接的客户端套接字 */
static int64_t last_recv_tick = 0;   /* 云端 TCP 上次收到数据的时间戳（毫秒） */
static int64_t s_wifi_trial_deadline_ms = 0; /* WiFi 试连截止时间（毫秒） */
static char device_sn[DEVICE_SN_MAX_LEN];    /* 设备序列号字符串 */
static device_runtime_config_t s_runtime_config;   /* 当前正在使用的网络配置 */
static device_runtime_config_t s_last_good_config; /* 上次连接成功的配置备份 */
static device_runtime_config_t s_pre_apply_config; /* 修改 CFG 前的配置快照（用于回滚） */
static bool s_pre_apply_config_valid = false;    /* 快照是否有效 */
static bool s_device_provisioned = false;        /* 是否已配网（对应 NVS wifi_prov） */

/* 将 SN 写入 NVS */
static esp_err_t save_sn_to_nvs(const char *sn);
/* 从 NVS 读取 SN */
static esp_err_t load_sn_from_nvs(char *sn, size_t len);
/* 从 NVS 加载网络配置与配网标志 */
static esp_err_t load_device_config_from_nvs(void);
/* 将网络配置保存到 NVS */
static esp_err_t save_device_config_to_nvs(void);
/* 设置/清除已配网标志并写入 NVS */
static esp_err_t set_device_provisioned(bool provisioned);
/* 查询是否已配网 */
static bool is_device_provisioned(void);
/* 加载编译期默认网络配置到 s_runtime_config */
static void load_default_device_config(void);
/* 安全初始化默认 NVS 与 device_nvs 分区 */
static void init_nvs_safe(void);
/* 一次性初始化 netif、事件循环、WiFi 栈 */
static void ensure_wifi_stack_initialized(void);
/* WiFi/IP 事件回调 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
/* 应用 STA 配置并切换（封装） */
static void apply_wifi_sta_config(void);
/* 配置 SoftAP 参数（热点名/密码等） */
static void apply_wifi_ap_config(void);
/* 切换到 AP 配网模式 */
static void switch_to_ap_config_mode(void);
/* 切换到 STA 工作模式 */
static void switch_to_sta_work_mode(void);
/* 停止 AP 本地 TCP 服务 */
static void stop_local_tcp_server(void);
/* 处理 CFG:* 配网命令 */
static bool handle_config_command(const char *input, char *output, int maxlen);
/* 处理 SYS:* 系统命令 */
static bool handle_system_command(const char *input, char *output, int maxlen);
/* 配网完成后请求切 STA 并重连 */
static void request_runtime_reconnect(bool wifi_changed, bool server_changed);
/* WiFi 试连超时则回滚配置 */
static void maybe_rollback_wifi_config(void);
/* AP 模式本地 TCP 服务任务 */
static void local_tcp_server_task(void *param);
/* 处理 AP 模式下手机发来的一行命令 */
static void process_local_tcp_command(int client_sock, char *line_buf, int *line_len);
/* 安全关闭 AP 本地 TCP 客户端连接 */
static void close_local_client_sock(int sock);

/* WiFi 模块统一日志输出（可变参数） */
void mywifi_log(const char *fmt, ...)
{
    va_list args; /* 可变参数列表 */
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/* 去掉字符串末尾的空格、制表符和换行 */
static void trim_line(char *text) /* text：待修剪的字符串（原地修改） */
{
    size_t len = strlen(text); /* 当前字符串长度 */
    while (len > 0)
    {
        char c = text[len - 1]; /* 末尾字符 */
        if (c != '\r' && c != '\n' && c != ' ' && c != '\t')
        {
            break;
        }
        text[--len] = '\0';
    }
}

/* 非空且长度合法时，把配置字符串拷贝到目标缓冲区 */
static bool copy_config_value(
    char *dest,        /* 目标缓冲区 */
    size_t dest_size,  /* 目标容量 */
    const char *value) /* 源字符串 */
{
    size_t len = strlen(value); /* 源字符串长度 */
    if (len == 0 || len >= dest_size)
    {
        return false;
    }

    memcpy(dest, value, len + 1);
    return true;
}

/* 加载编译期默认网络配置到 s_runtime_config */
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

/* 从 NVS 读取字符串；失败则保留 buffer 原内容 */
static void load_nvs_str_or_default(
    nvs_handle_t nvs,      /* 已打开的 NVS 句柄 */
    const char *key,       /* 键名 */
    char *buffer,          /* 输出缓冲区 */
    size_t buffer_len)     /* 缓冲区容量 */
{
    size_t required = buffer_len; /* NVS API 需要的长度入参 */
    if (nvs_get_str(nvs, key, buffer, &required) != ESP_OK)
    {
        return;
    }
    buffer[buffer_len - 1] = '\0';
}

/* 从 NVS 读出网络配置到 s_runtime_config，并加载 wifi_prov 配网标志 */
static esp_err_t load_device_config_from_nvs(void)
{
    nvs_handle_t nvs;                  /* NVS 句柄 */
    uint16_t saved_port = 0;           /* 从 NVS 读出的云端端口 */
    esp_err_t err;                     /* NVS 操作返回值 */
    bool should_persist_defaults = false; /* 是否需要把恢复的默认密码写回 NVS */

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

    {
        uint8_t provisioned = 0; /* NVS 中读出的配网标志 0/1 */
        if (nvs_get_u8(nvs, DEVICE_PROVISIONED_KEY, &provisioned) == ESP_OK)
        {
            s_device_provisioned = provisioned != 0;
        }
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
        esp_err_t save_err = save_device_config_to_nvs(); /* 写回默认密码的保存结果 */
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

/* 把 s_runtime_config 写入 NVS（路由器 + 云端地址） */
static esp_err_t save_device_config_to_nvs(void)
{
    nvs_handle_t nvs; /* NVS 句柄 */
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

static esp_err_t set_device_provisioned(bool provisioned) /* provisioned：true 表示已完成配网 */
{
    nvs_handle_t nvs; /* NVS 句柄 */
    esp_err_t err = nvs_open_from_partition(DEVICE_NVS_PART, DEVICE_NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_u8(nvs, DEVICE_PROVISIONED_KEY, provisioned ? 1 : 0);
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    if (err == ESP_OK)
    {
        s_device_provisioned = provisioned;
    }
    return err;
}

static bool is_device_provisioned(void)
{
    return s_device_provisioned;
}

/* 安全初始化默认 NVS 分区与 device_nvs 专用分区 */
static void init_nvs_safe(void)
{
    esp_err_t err = nvs_flash_init(); /* 默认 NVS 初始化结果 */
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

/* 一次性初始化网络接口、事件循环和 WiFi 协议栈 */
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

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); /* WiFi 默认初始化参数 */
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    s_wifi_stack_initialized = true;
}

/* 配置手机配网用的 SoftAP（热点名、密码、信道等） */
static void apply_wifi_ap_config(void)
{
    wifi_config_t ap_config = {0}; /* AP 模式 WiFi 配置结构体 */

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

/* 关闭 AP 本地 TCP 客户端套接字（避免与 stop 逻辑重复关闭） */
static void close_local_client_sock(int sock) /* sock：待关闭的客户端套接字 */
{
    if (sock < 0 || s_local_client_sock != sock)
    {
        return;
    }

    s_local_client_sock = -1;
    shutdown(sock, SHUT_RDWR);
    close(sock);
}

/* 关闭 AP 本地 TCP（切 STA 前调用，避免端口占用） */
static void stop_local_tcp_server(void)
{
    s_local_tcp_server_stop_requested = true;

    if (s_local_client_sock >= 0)
    {
        close_local_client_sock(s_local_client_sock);
    }

    if (s_local_listen_sock >= 0)
    {
        int listen_sock = s_local_listen_sock; /* 暂存监听套接字再关闭 */
        s_local_listen_sock = -1;
        shutdown(listen_sock, SHUT_RDWR);
        close(listen_sock);
    }

    /* 等待本地 TCP 任务自行退出，避免在此处强删导致竞态 */
    for (int i = 0; i < 100 && s_local_tcp_server_task_handle != NULL; i++) /* i：等待任务退出的轮询次数 */
    {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (s_local_tcp_server_task_handle != NULL)
    {
        ESP_LOGW(TAG, "Local TCP server task stuck, forcing delete");
        vTaskDelete(s_local_tcp_server_task_handle);
        s_local_tcp_server_task_handle = NULL;
    }

    s_local_tcp_server_stop_requested = false;
}

/* 进入 AP 配网：开热点 ESP-WASH，启动 192.168.4.1:9000 本地 TCP */
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

/* 进入 STA 工作：连 s_runtime_config 里的路由器，之后由 tcp_client 连云端 */
static void switch_to_sta_work_mode(void)
{
    wifi_config_t wifi_config = {0}; /* STA 模式 WiFi 配置结构体 */

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

/*
 * WiFi 事件：STA 启动/断线/拿到 IP；AP 启动时拉起本地 TCP 服务
 * 拿到 IP 后若处于试连期，则把当前配置记为“上次成功配置”
 */
static void wifi_event_handler(
    void *arg,                    /* 用户参数（未使用） */
    esp_event_base_t event_base,  /* 事件大类：WIFI_EVENT / IP_EVENT */
    int32_t event_id,             /* 具体事件 ID */
    void *event_data)             /* 事件附带数据 */
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

/* 应用当前 STA 配置（内部调用 switch_to_sta_work_mode） */
static void apply_wifi_sta_config(void)
{
    switch_to_sta_work_mode();
}

/* 创建云端 TCP 用的收发队列 */
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

/*
 * 上电初始化 WiFi（函数名历史遗留叫 sta，实际按是否配网分支）
 * 未配网 → AP；已配网 → 直接 STA
 */
static void wifi_init_mode(void)
{
    if (wifi_event_group == NULL)
    {
        wifi_event_group = xEventGroupCreate();
    }

    ensure_wifi_stack_initialized();
    if (is_device_provisioned())
    {
        switch_to_sta_work_mode();
        ESP_LOGI(TAG, "wifi_init finished (STA work mode, provisioned)");
    }
    else
    {
        switch_to_ap_config_mode();
        ESP_LOGI(TAG, "wifi_init finished (AP config mode)");
    }
}

/* 进入 OTA 模式：等待 WiFi 有 IP 后启动 HTTP OTA 任务 */
void wifi_ota_mode_start(const char *default_url) /* default_url：OTA 固件 URL，可为 NULL */
{
    ESP_LOGI(TAG, "Entering OTA mode");

    if (!s_wifi_started)
    {
        wifi_init_mode();
        s_wifi_ota_started_by_me = true;
    }

    EventBits_t bits = xEventGroupWaitBits( /* 等待 STA 获 IP 的事件位 */
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

    http_ota_config_t cfg = {0}; /* HTTP OTA 任务参数 */
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

/* 退出 OTA 模式并释放本模块占用的 WiFi/OTA 资源 */
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

/* SYS:MODE=STA / CFG:APPLY 后：切 STA，并按需标记 WiFi 试连或云端重连 */
static void request_runtime_reconnect(
    bool wifi_changed,   /* 路由器 SSID/密码是否变更 */
    bool server_changed) /* 云端 IP/端口是否变更 */
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

/* 试连新 WiFi 超过 30s 仍未连上 → 恢复旧配置并退回 AP 配网 */
static void maybe_rollback_wifi_config(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000; /* 当前时间（毫秒） */

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

/*
 * 系统命令 SYS:*
 *   SYS:MODE=STA — 保存配置、标记已配网、切 STA
 *   SYS:MODE=AP  — 退回配网热点
 *   SYS:STATUS   — 查模式 / WiFi / 云端 TCP / 试连 / 是否已配网
 */
static bool handle_system_command(
    const char *input,  /* 完整命令行，如 SYS:STATUS */
    char *output,       /* 应答写入缓冲区 */
    int maxlen)         /* output 最大容量 */
{
    if (strncmp(input, "SYS:", 4) != 0)
    {
        return false;
    }

    if (strcmp(input, "SYS:MODE=STA") == 0)
    {
        esp_err_t err = save_device_config_to_nvs(); /* 保存网络配置到 NVS */
        if (err != ESP_OK)
        {
            snprintf(output, maxlen, "SYS:ERR,MODE\r\n");
            return true;
        }

        err = set_device_provisioned(true); /* 标记已配网并写 NVS */
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
            "SYS:STATUS,MODE=%s,STA=%s,TCP=%s,TRIAL=%s,PROVISIONED=%s\r\n",
            s_run_mode == WIFI_RUN_MODE_STA_WORK ? "STA" : "AP",
            s_sta_connected ? "CONNECTED" : "DISCONNECTED",
            tcp_connected ? "CONNECTED" : "DISCONNECTED",
            s_wifi_trial_active ? "ACTIVE" : "IDLE",
            s_device_provisioned ? "YES" : "NO");
        return true;
    }

    snprintf(output, maxlen, "SYS:ERR,UNKNOWN\r\n");
    return true;
}

/*
 * 配网命令 CFG:*（与蓝牙相同）
 *   WIFI_SSID / WIFI_PASSWORD — 路由器
 *   SERVER_IP / SERVER_PORT   — 云端 TCP
 *   GET / SAVE / APPLY / CANCEL
 */
static bool handle_config_command(
    const char *input,  /* 完整命令行，如 CFG:WIFI_SSID=xxx */
    char *output,       /* 应答写入缓冲区 */
    int maxlen)         /* output 最大容量 */
{
    const char *value = NULL; /* 指向命令中“=”后面的参数值 */

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
        char *end = NULL; /* strtol 解析结束位置 */
        long port = 0;    /* 解析出的端口号 */

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
        esp_err_t err = save_device_config_to_nvs(); /* 保存到 NVS 的结果 */
        snprintf(
            output,
            maxlen,
            err == ESP_OK ? "CFG:OK,SAVED\r\n" : "CFG:ERR,SAVE\r\n");
        return true;
    }

    if (strcmp(input, "CFG:APPLY") == 0)
    {
        bool handled = handle_system_command("SYS:MODE=STA", output, maxlen); /* 是否成功切 STA */
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

/* 供 ctrl_protocol 调用：先 CFG 再 SYS，与 BLE/本地 TCP/云端共用 */
bool wifi_module_handle_config_command(
    const char *input,  /* 命令字符串 */
    char *output,       /* 应答缓冲区 */
    int maxlen)         /* 应答缓冲区大小 */
{
    if (handle_config_command(input, output, maxlen))
    {
        return true;
    }

    return handle_system_command(input, output, maxlen);
}

/* 向指定套接字发送应答；prefix 非空时拼在应答前（本地 TCP 通常传 NULL） */
static void send_response_with_prefix(
    int sock,              /* 目标套接字 */
    const char *prefix,    /* 可选前缀（一般为 NULL） */
    const char *response)  /* ctrl_protocol 应答正文 */
{
    char tx_buf[QUEUE_ITEM_SIZE] = {0}; /* 待发送缓冲区 */

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

    size_t len = strlen(tx_buf); /* 待发数据长度 */
    if (len == 0)
    {
        return;
    }

    if (tx_buf[len - 1] != '\n' && len < sizeof(tx_buf) - 1) /* 末尾无换行则补 \n */
    {
        tx_buf[len++] = '\n';
        tx_buf[len] = '\0';
    }

    send(sock, tx_buf, len, 0);
}

/* AP 模式：收到一行 → ctrl_protocol → 原样回给手机（与蓝牙一致） */
static void process_local_tcp_command(
    int client_sock,   /* 手机 TCP 连接套接字 */
    char *line_buf,    /* 行缓冲 */
    int *line_len)     /* 行缓冲当前长度（入参/出参） */
{
    char response[QUEUE_ITEM_SIZE] = {0}; /* ctrl_protocol 输出缓冲区 */

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

/* 云端可选保活行，不进 ctrl_protocol（业务命令请直接发 SYS:/CMD:/CFG:） */
static bool should_ignore_server_line(const char *line) /* line：云端发来的一行文本 */
{
    return strcmp(line, "REG,OK") == 0 || strcmp(line, "PING") == 0 || strcmp(line, "PONG") == 0;
}

/*
 * 云端 TCP 客户端任务（仅 STA 模式运行）
 * 循环：等 WiFi 有 IP → connect 云端 → 按行收命令入 rx 队列 → 从 tx 队列发应答
 */
void tcp_client_task(void *param) /* param：FreeRTOS 任务参数（未使用） */
{
    char rx_buf[256];  /* 云端 TCP 单次 recv 缓冲区 */
    char line_buf[256]; /* 按行组包缓冲区 */
    int line_len = 0;   /* line_buf 中已累积的字节数 */

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

        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(1000)); /* 等 STA 有 IP */
        if ((bits & WIFI_CONNECTED_BIT) == 0)
        {
            continue;
        }

        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP); /* 新建云端 TCP 套接字 */
        if (sock < 0)
        {
            ESP_LOGE(TAG, "socket create failed, errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        struct sockaddr_in server_addr = { /* 云端服务器地址 */
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

        while (1)
        {
            fd_set rfds;                              /* select 读集合 */
            struct timeval tv = {.tv_sec = 1, .tv_usec = 0}; /* select 超时 1 秒 */

            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);

            int ret = select(sock + 1, &rfds, NULL, NULL, &tv); /* 等待可读或超时 */
            if (ret < 0)
            {
                ESP_LOGE(TAG, "select failed, errno=%d", errno);
                break;
            }

            if (ret > 0 && FD_ISSET(sock, &rfds))
            {
                int len = recv(sock, rx_buf, sizeof(rx_buf), 0); /* 本次收到的字节数 */
                if (len <= 0)
                {
                    break;
                }

                last_recv_tick = esp_timer_get_time() / 1000;

                for (int i = 0; i < len; i++) /* i：本次 recv 数据中的字节下标 */
                {
                    char c = rx_buf[i]; /* 当前处理的字符 */
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
                            wifi_data_t pkt = {0}; /* 入队给 wifi_protocol_task 的命令包 */
                            strncpy((char *)pkt.buf, line_buf, QUEUE_ITEM_SIZE - 1);
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

            wifi_data_t tx = {0}; /* 从发送队列取出的应答 */
            if (xQueueReceive(wifi_tx_queue, &tx, 0) == pdTRUE)
            {
                send(sock, tx.buf, tx.len, 0);
            }

            if (s_reconnect_requested) /* 配置变更等需要重连云端 */
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

/* AP 配网：在 192.168.4.1:9000 监听，手机 TCP 连入后走 process_local_tcp_command */
static void local_tcp_server_task(void *param) /* param：FreeRTOS 任务参数（未使用） */
{
    struct sockaddr_in server_addr = {0};  /* 本机监听地址 */
    struct sockaddr_in client_addr = {0};  /* 客户端地址（accept 填充） */
    socklen_t client_addr_len = sizeof(client_addr); /* client_addr 长度 */
    int listen_sock = -1;  /* 监听套接字 */
    int client_sock = -1;  /* 当前连接的客户端套接字 */
    char rx_buf[QUEUE_ITEM_SIZE] = {0}; /* recv 缓冲区 */
    char line_buf[QUEUE_ITEM_SIZE] = {0}; /* 按行组包缓冲区 */
    int line_len = 0; /* line_buf 已累积长度 */

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

    int reuse = 1; /* 端口复用，避免重启后 bind 失败 */
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
        if (client_sock < 0)
        {
            if (s_local_tcp_server_stop_requested || s_run_mode != WIFI_RUN_MODE_AP_CONFIG)
            {
                break;
            }
            ESP_LOGE(TAG, "Local TCP server accept failed: errno=%d", errno);
            continue;
        }

        s_local_client_sock = client_sock;

        char client_ip[INET_ADDRSTRLEN] = {0}; /* 客户端 IP 字符串 */
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        ESP_LOGI(TAG, "Local TCP client connected: %s:%u", client_ip, ntohs(client_addr.sin_port));
        line_len = 0;

        while (1)
        {
            if (s_local_tcp_server_stop_requested)
            {
                break;
            }

            int len = recv(client_sock, rx_buf, sizeof(rx_buf), 0); /* 本次收到字节数 */
            if (len <= 0)
            {
                ESP_LOGW(TAG, "Local TCP client disconnected");
                break;
            }

            for (int i = 0; i < len; i++) /* i：本次 recv 数据中的字节下标 */
            {
                char c = rx_buf[i]; /* 当前字符 */
                if (c == '\r')
                {
                    continue;
                }

                if (c == '\n')
                {
                    process_local_tcp_command(client_sock, line_buf, &line_len);
                    continue;
                }

                if (line_len < (int)sizeof(line_buf) - 1)
                {
                    line_buf[line_len++] = c;
                }
            }
        }

        close_local_client_sock(client_sock);
        client_sock = -1;
    }

    if (listen_sock >= 0)
    {
        if (s_local_listen_sock == listen_sock)
        {
            s_local_listen_sock = -1;
        }
        shutdown(listen_sock, SHUT_RDWR);
        close(listen_sock);
    }
    s_local_client_sock = -1;
    s_local_tcp_server_task_handle = NULL;
    vTaskDelete(NULL);
}

/*
 * 云端协议任务：从 rx 队列取命令 → ctrl_protocol → 应答原样放入 tx 队列
 * 与 ble_receive_task 逻辑一致，不做 ACK| 包装
 */
void wifi_protocol_task(void *param) /* param：FreeRTOS 任务参数（未使用） */
{
    wifi_data_t rx;        /* 从 rx 队列取出的云端命令 */
    char response[256];    /* ctrl_protocol 应答缓冲区 */

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

        wifi_data_t tx = {0}; /* 放入 tx 队列、由 tcp_client 发往云端 */
        strncpy((char *)tx.buf, response, QUEUE_ITEM_SIZE - 1);
        tx.buf[QUEUE_ITEM_SIZE - 1] = '\0';
        tx.len = strnlen((char *)tx.buf, QUEUE_ITEM_SIZE);

        if (xQueueSend(wifi_tx_queue, &tx, pdMS_TO_TICKS(10)) != pdTRUE)
        {
            ESP_LOGW(TAG, "WiFi TX queue full, response dropped");
        }
        else
        {
            ESP_LOGI(TAG, "TCP TX: %s", tx.buf);
        }
    }
}

/* 从 device_nvs 读取设备 SN */
static esp_err_t load_sn_from_nvs(char *sn, size_t len) /* sn：输出缓冲；len：缓冲容量 */
{
    nvs_handle_t nvs;              /* NVS 句柄 */
    size_t required_len = len;     /* SN 缓冲区容量（NVS API 入参） */
    esp_err_t err = nvs_open_from_partition(DEVICE_NVS_PART, DEVICE_NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_get_str(nvs, DEVICE_SN_KEY, sn, &required_len);
    nvs_close(nvs);
    return err;
}

/* 将设备 SN 写入 device_nvs */
static esp_err_t save_sn_to_nvs(const char *sn) /* sn：要保存的序列号字符串 */
{
    nvs_handle_t nvs; /* NVS 句柄 */
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

/* 用 WiFi STA 网卡 MAC 生成默认 SN（格式 SN_XXXXXXXXXXXX） */
static void generate_sn(char *sn, size_t len) /* sn：输出缓冲；len：缓冲容量 */
{
    uint8_t mac[6] = {0}; /* STA 接口 MAC 地址 6 字节 */
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(sn, len, "SN_%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* 初始化设备 SN：优先读 NVS，没有则按 MAC 生成并保存 */
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

/*
 * 模块入口（main 里调用）
 * 顺序：NVS → SN → 读配置 → 队列 → 按配网状态开 AP/STA → 起云端协议任务 + TCP 客户端
 * 本地 TCP 服务在 switch_to_ap_config_mode / AP_START 事件里按需创建
 */
void wifi_tcp_start(void)
{
    init_nvs_safe();
    init_device_sn();
    load_device_config_from_nvs();
    wifi_module_queue_init();
    wifi_init_mode();

    xTaskCreate(wifi_protocol_task, "wifi_proto", 4096, NULL, 6, NULL);
    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
}
