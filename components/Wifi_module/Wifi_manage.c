#include <string.h>     // 字符串处理函数，如 strlen, strcmp, strtok 等
#include <errno.h>      // 错误号处理
#include <sys/socket.h> // socket API
#include <netdb.h>      // 网络数据库操作
#include <unistd.h>     // UNIX 标准函数，如 close()

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h" // FreeRTOS 事件组，用于等待 WiFi 连接

#include "esp_wifi.h"  // WiFi 功能
#include "esp_event.h" // ESP32 事件循环
#include "esp_netif.h" // 网络接口
#include "esp_log.h"   // 日志打印
#include "nvs_flash.h" // 非易失性存储初始化

#include "esp_timer.h" // 高精度定时器
#include "esp_mac.h"   // MAC 地址
#include "nvs.h"

#include "driver/gpio.h"

static esp_err_t save_sn_to_nvs(const char *sn);
static volatile bool tcp_connected = false;
static volatile bool sn_updated = false;

/* =================== 用户配置 =================== */
// WiFi SSID 和密码
#define WIFI_SSID "JT-13F"
#define WIFI_PASSWORD "jt123456"

// 云服务器 IP 和端口
#define SERVER_IP "192.168.172.107"
#define SERVER_PORT 9000

#define DEVICE_NVS_PART "device_nvs"
#define DEVICE_NVS_NS "device"
#define DEVICE_SN_KEY "sn"

// 设备唯一标识
// static char device_id[32];
// #define DEVICE_ID "ESP32S3_001"
#define DEVICE_SN_MAX_LEN 32
static char device_sn[DEVICE_SN_MAX_LEN];

// 心跳间隔（毫秒）
#define HEARTBEAT_INTERVAL_MS 5000
// 心跳超时时间（毫秒），超过该时间没收到 PONG 就重连
#define HEARTBEAT_TIMEOUT_MS 15000
/* =============================================== */

static const char *TAG = "WIFI_TCP"; // 日志 TAG，用于 ESP_LOG*

/* WiFi 事件标志位 */
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0 // WiFi 已连接标志位

/* TCP socket 描述符 */
static int tcp_sock = -1; // socket 文件描述符，初始化为无效

/* 上一次收到数据的时间戳（毫秒） */
static int64_t last_recv_tick = 0;

/* =================== WiFi 事件回调 =================== */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    // 判断事件类型
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        // WiFi 启动成功后，立即尝试连接 WiFi
        esp_wifi_connect();
    }   
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        // WiFi 断开
        ESP_LOGW(TAG, "WiFi disconnected, retry..."); // 打印警告日志
        esp_wifi_connect();                           // 自动重连
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        // 成功获取 IP 地址
        ESP_LOGI(TAG, "WiFi connected"); // 打印信息日志
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        // 设置事件组标志，通知其他任务 WiFi 已连接
    }
}

static void init_nvs_safe(void)
{
    esp_err_t err;

    // 初始化默认 nvs（WiFi 仍然要用）
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // 初始化 device_nvs（你的 SN 专用）
    err = nvs_flash_init_partition("device_nvs");
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase_partition("device_nvs"));
        ESP_ERROR_CHECK(nvs_flash_init_partition("device_nvs"));
    }

    ESP_LOGI(TAG, "NVS init OK (default + device_nvs)");
}

/* =================== WiFi 初始化 =================== */
static void wifi_init_sta(void)
{
    // 创建事件组，用于等待 WiFi 连接完成
    wifi_event_group = xEventGroupCreate();

    // 初始化 NVS，用于 WiFi 存储
    // ESP_ERROR_CHECK(nvs_flash_init());
    init_nvs_safe();

    // 初始化 TCP/IP 网络接口
    ESP_ERROR_CHECK(esp_netif_init());

    // 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 创建默认 WiFi STA（客户端）接口
    esp_netif_create_default_wifi_sta();

    // 初始化 WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册 WiFi 和 IP 事件回调
    esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    // 配置 WiFi SSID 和密码
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // 设置为 STA 模式
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start()); // 启动 WiFi

    ESP_LOGI(TAG, "wifi_init_sta finished");
}

/* =================== TCP 发送函数 =================== */
static void tcp_send(const char *data)
{
    if (tcp_sock >= 0)
    { // 判断 socket 是否有效
        // 发送数据到 TCP 服务器
        send(tcp_sock, data, strlen(data), 0);
    }
}

/* =================== 指令处理 =================== */
static void handle_cmd(const char *cmd)
{
    ESP_LOGI(TAG, "RX: %s", cmd);

    // /* ===== 心跳 ===== */
    // if (strcmp(cmd, "PONG") == 0)
    // {
    //     last_recv_tick = esp_timer_get_time() / 1000;
    //     return;
    // }

    /* ===== 非 CMD 指令 ===== */
    if (strncmp(cmd, "CMD|", 4) != 0)
    {
        tcp_send("ACK|UNKNOWN|ERR\n");
        return;
    }

    const char *payload = cmd + 4;

    /* ===== SET_SN ===== */
    if (strncmp(payload, "SET_SN|", 7) == 0)
    {
        char new_sn[DEVICE_SN_MAX_LEN];
        strncpy(new_sn, payload + 7, DEVICE_SN_MAX_LEN - 1);
        new_sn[DEVICE_SN_MAX_LEN - 1] = 0;

        /* 去掉 \r \n */
        char *p = new_sn;
        while (*p)
        {
            if (*p == '\r' || *p == '\n')
            {
                *p = 0;
                break;
            }
            p++;
        }
        ESP_LOGI(TAG, "RAW new_sn len=%d [%s]", strlen(new_sn), new_sn);

        if (strlen(new_sn) == 0 || strlen(new_sn) >= DEVICE_SN_MAX_LEN)
        {
            tcp_send("ACK|SET_SN|INVALID\n");
            return;
        }

        if (save_sn_to_nvs(new_sn) == ESP_OK)
        {
            strncpy(device_sn, new_sn, DEVICE_SN_MAX_LEN - 1);
            device_sn[DEVICE_SN_MAX_LEN - 1] = 0;

            sn_updated = true; // ⭐ 核心
            tcp_send("ACK|SET_SN|OK\n");

            ESP_LOGW(TAG, "SN updated to %s, will reconnect", device_sn);
        }
        else
        {
            tcp_send("ACK|SET_SN|FAIL\n");
        }
        return;
    }

    /* ===== LED_ON ===== */
    if (strcmp(payload, "LED_ON") == 0)
    {
        // led_on(); // 你的硬件函数
        gpio_set_level(GPIO_NUM_14, 1);
        tcp_send("ACK|LED_ON|OK\n");
        return;
    }

    /* ===== LED_OFF ===== */
    if (strcmp(payload, "LED_OFF") == 0)
    {
        gpio_set_level(GPIO_NUM_14, 0);
        tcp_send("ACK|LED_OFF|OK\n");
        return;
    }

    tcp_send("ACK|UNKNOWN|ERR\n");
}

/* =================== TCP 客户端任务 =================== */
static void tcp_client_task(void *arg)
{
    char rx_buf[256];
    char line_buf[256];
    int line_len = 0;

    /* 等待 WiFi 连接 */
    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        false,
        true,
        portMAX_DELAY);

    while (1)
    {
        struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(SERVER_PORT),
            .sin_addr.s_addr = inet_addr(SERVER_IP),
        };

        tcp_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (tcp_sock < 0)
        {
            ESP_LOGE(TAG, "Create socket failed");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "Connecting to server...");
        if (connect(tcp_sock,
                    (struct sockaddr *)&server_addr,
                    sizeof(server_addr)) != 0)
        {
            ESP_LOGE(TAG, "Connect failed");
            close(tcp_sock);
            tcp_sock = -1;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        tcp_connected = true;
        ESP_LOGI(TAG, "TCP connected");

        /* ===== REG ===== */
        char reg_msg[64];
        snprintf(reg_msg, sizeof(reg_msg),
                 "REG|%s|1.0.0\n", device_sn);
        tcp_send(reg_msg);

        int64_t now = esp_timer_get_time() / 1000;
        last_recv_tick = now;
        int64_t last_heartbeat = now;

        memset(line_buf, 0, sizeof(line_buf));
        line_len = 0;

        while (tcp_connected)
        {
            fd_set read_fds;
            struct timeval tv;

            FD_ZERO(&read_fds);
            FD_SET(tcp_sock, &read_fds);

            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int ret = select(tcp_sock + 1, &read_fds, NULL, NULL, &tv);
            if (ret > 0 && FD_ISSET(tcp_sock, &read_fds))
            {
                int len = recv(tcp_sock, rx_buf, sizeof(rx_buf), 0);
                if (len <= 0)
                {
                    ESP_LOGW(TAG, "Server disconnected");
                    break;
                }

                for (int i = 0; i < len; i++)
                {
                    char c = rx_buf[i];

                    if (c == '\n')
                    {
                        line_buf[line_len] = 0;

                        /* ===== PONG ===== */
                        if (strcmp(line_buf, "PONG") == 0)
                        {
                            last_recv_tick = esp_timer_get_time() / 1000;
                        }
                        else
                        {
                            handle_cmd(line_buf);
                        }

                        line_len = 0;
                        memset(line_buf, 0, sizeof(line_buf));
                    }
                    else if (line_len < sizeof(line_buf) - 1)
                    {
                        line_buf[line_len++] = c;
                    }
                }

                last_recv_tick = esp_timer_get_time() / 1000;
            }

            now = esp_timer_get_time() / 1000;

            /* ===== 心跳 ===== */
            if (now - last_heartbeat >= HEARTBEAT_INTERVAL_MS)
            {
                tcp_send("PING\n");
                last_heartbeat = now;
            }

            /* ===== SN 已修改，安全重连 ===== */
            if (sn_updated)
            {
                ESP_LOGW(TAG, "SN updated, reconnecting...");
                sn_updated = false;

                /* 给 ACK 一点时间真正发出去 */
                vTaskDelay(pdMS_TO_TICKS(200));
                break;
            }

            /* ===== 心跳超时 ===== */
            if (now - last_recv_tick >= HEARTBEAT_TIMEOUT_MS)
            {
                ESP_LOGW(TAG, "Heartbeat timeout");
                break;
            }
        }

        /* ===== 清理 socket ===== */
        tcp_connected = false;

        if (tcp_sock >= 0)
        {
            shutdown(tcp_sock, SHUT_RDWR);
            close(tcp_sock);
            tcp_sock = -1;
        }

        ESP_LOGI(TAG, "Reconnect in 2s...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}


static esp_err_t load_sn_from_nvs(char *sn, size_t len)
{
    nvs_handle_t nvs;
    size_t required_len = len;

    esp_err_t err = nvs_open_from_partition(
        "device_nvs",
        "device",
        NVS_READONLY,
        &nvs);
    if (err != ESP_OK)
        return err;

    err = nvs_get_str(nvs, "sn", sn, &required_len);
    nvs_close(nvs);
    return err;
}

static esp_err_t save_sn_to_nvs(const char *sn)
{
    nvs_handle_t nvs;

    esp_err_t err = nvs_open_from_partition(
        "device_nvs",
        "device",
        NVS_READWRITE,
        &nvs);
    if (err != ESP_OK)
        return err;

    err = nvs_set_str(nvs, "sn", sn);
    if (err == ESP_OK)
        err = nvs_commit(nvs);

    nvs_close(nvs);
    return err;
}

static void generate_sn(char *sn, size_t len)
{
    uint64_t mac;
    esp_read_mac((uint8_t *)&mac, ESP_MAC_WIFI_STA);

    snprintf(sn, len,
             "SN_%02X%02X%02X%02X%02X%02X",
             (uint8_t)(mac >> 40),
             (uint8_t)(mac >> 32),
             (uint8_t)(mac >> 24),
             (uint8_t)(mac >> 16),
             (uint8_t)(mac >> 8),
             (uint8_t)(mac));
}

static void init_device_sn(void)
{
    esp_err_t err;

    memset(device_sn, 0, sizeof(device_sn));

    err = load_sn_from_nvs(device_sn, sizeof(device_sn));
    if (err == ESP_OK)
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

/* =================== 对外接口 =================== */
void wifi_tcp_start(void)
{
    // init_nvs_safe();
    init_device_sn(); // 先初始化 SN（NVS）
    wifi_init_sta();  // 再启动 WiFi
    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
    // 创建 TCP 客户端任务
}
