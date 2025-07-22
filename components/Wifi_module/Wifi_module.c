#include "Wifi_module.h" // 包含自定义的WiFi模块头文件
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#define TAG "wifi_module" // 日志标签，用于打印日志时区分模块

#define WIFI_CONNECTED_BIT BIT0 // WiFi连接状态事件标志位
// 全局变量声明
static EventGroupHandle_t
    wifi_event_group; // WiFi事件组句柄，用于同步WiFi连接状态
static wifi_mode_t current_mode =
    WIFI_MODE_APSTA; // 当前WiFi工作模式，默认AP+STA共存

static wifi_config_t sta_config = {0}; // STA模式配置结构体，初始化为全0
static wifi_config_t ap_config = {0};  // AP模式配置结构体，初始化为全0

// TCP服务器相关变量
static int tcp_server_sock = -1; // TCP服务器监听socket描述符，-1表示未创建
static int tcp_port = 1234;      // TCP服务器监听端口，默认1234
static TaskHandle_t tcp_server_task_handle =
    NULL; // TCP服务器任务句柄，用于管理TCP服务任务

// UART相关变量
#define EX_UART_NUM UART_NUM_0 // 串口号，这里使用UART0
#define BUF_SIZE 1024          // UART接收缓冲区大小
#define UART_BAUD_RATE 115200

// Wifi事件组位定义
static QueueHandle_t wifi_tx_queue = NULL; // WiFi发送队列句柄
static QueueHandle_t wifi_rx_queue = NULL; // WiFi接收队列句柄

#define QUEUE_ITEM_SIZE 256 // 队列中每个数据项的大小，这里设置为256字节

// 定义WiFi数据结构体，用于存储发送和接收的数据
typedef struct
{
    uint8_t buf[QUEUE_ITEM_SIZE];
    size_t len;
} wifi_data_t;

/************************** WiFi事件处理回调函数 **************************/
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_AP_STACONNECTED:
        { // 有设备连接到AP
            wifi_event_ap_staconnected_t *event = event_data;
            ESP_LOGI(TAG, "AP: Client connected: " MACSTR ", AID=%d",
                     MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
        { // 有设备断开AP连接
            wifi_event_ap_stadisconnected_t *event = event_data;
            ESP_LOGI(TAG, "AP: Client disconnected: " MACSTR ", AID=%d",
                     MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_STA_START: // STA模式启动，开始连接
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: // STA模式断开，自动重连
            ESP_LOGI(TAG, "STA disconnected. Reconnecting...");
            esp_wifi_connect();
            break;
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        // STA成功获取到IP地址
        ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "STA Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        // 设置事件组位，表示WiFi已连接
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/************************** 启动WiFi模式（AP/STA/APSTA）
 * **************************/
static void start_wifi_mode(wifi_mode_t mode)
{
    ESP_LOGI(TAG, "Starting WiFi mode %s%s%s", (mode & WIFI_MODE_AP) ? "AP " : "",
             (mode & WIFI_MODE_STA) ? "STA " : "",
             (!(mode & WIFI_MODE_AP) && !(mode & WIFI_MODE_STA)) ? "NONE" : "");

    ESP_ERROR_CHECK(esp_wifi_stop());         // 停止当前WiFi服务
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode)); // 设置新的WiFi模式

    if (mode & WIFI_MODE_AP)
    {
        // 配置AP模式参数
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    }
    if (mode & WIFI_MODE_STA)
    {
        // 配置STA模式参数
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    }
    ESP_ERROR_CHECK(esp_wifi_start()); // 启动WiFi服务

    current_mode = mode; // 保存当前模式
}

/************************** TCP服务器任务/兼信息回调 **************************/
static void tcp_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr; // 服务器地址
    struct sockaddr_in client_addr; // 客户端地址
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock = -1;

    ESP_LOGI(TAG, "Starting TCP server on port %d", tcp_port);

    // 创建TCP socket
    tcp_server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (tcp_server_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    // 绑定socket到端口和任意IP
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(tcp_port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(tcp_server_sock, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) != 0)
    {
        ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
        close(tcp_server_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(tcp_server_sock, 5) != 0)
    {
        ESP_LOGE(TAG, "Socket listen failed: errno %d", errno);
        close(tcp_server_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP server listening");

    char rx_buffer[128];

    while (1)
    {
        ESP_LOGI(TAG, "Waiting for a client to connect...");
        client_sock = accept(tcp_server_sock, (struct sockaddr *)&client_addr,
                             &client_addr_len);
        if (client_sock < 0)
        {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // 获取客户端IP地址字符串
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        ESP_LOGI(TAG, "Client connected from %s:%d", client_ip, client_port);

        // 通信循环
        while (1)
        {
            int len = recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len < 0)
            {
                ESP_LOGE(TAG, "recv failed from %s:%d, errno %d", client_ip,
                         client_port, errno);
                break;
            }
            else if (len == 0)
            {
                ESP_LOGI(TAG, "Client %s:%d closed connection", client_ip, client_port);
                break;
            }
            else
            {
                rx_buffer[len] = '\0';
                ESP_LOGI(TAG, "Received %d bytes from %s:%d: %s", len, client_ip,
                         client_port, rx_buffer);

                // 把接收到的数据放入接收队列
                char *msg = malloc(len + 1);
                if (msg)
                {
                    strcpy(msg, rx_buffer);
                    xQueueSend(wifi_rx_queue, &msg, portMAX_DELAY);
                }

                // 检查是否有要发送的数据
                char *tx_msg = NULL;
                if (xQueueReceive(wifi_tx_queue, &tx_msg, pdMS_TO_TICKS(100)) ==
                    pdTRUE)
                {
                    if (tx_msg)
                    {
                        int sent = send(client_sock, tx_msg, strlen(tx_msg), 0);
                        ESP_LOGI(TAG, "Sent %d bytes to %s:%d: %s", sent, client_ip,
                                 client_port, tx_msg);
                        free(tx_msg);
                    }
                }
            }
        }

        ESP_LOGI(TAG, "Closing connection with client %s:%d", client_ip,
                 client_port);
        close(client_sock);
    }

    close(tcp_server_sock);
    tcp_server_sock = -1;
    ESP_LOGI(TAG, "TCP server task ended");
    vTaskDelete(NULL);
}

/**
 * @brief 停止TCP服务器任务和关闭socket
 */
static void tcp_server_stop()
{
    if (tcp_server_task_handle)
    {
        vTaskDelete(tcp_server_task_handle); // 删除TCP服务器任务
        tcp_server_task_handle = NULL;
    }
    if (tcp_server_sock != -1)
    {
        close(tcp_server_sock); // 关闭socket
        tcp_server_sock = -1;
    }
}

/**
 * @brief 启动TCP服务器任务（如果未运行）
 */
static void tcp_server_start()
{
    if (tcp_server_task_handle == NULL)
    {
        xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5,
                    &tcp_server_task_handle);
    }
}

/************************** 串口命令任务 **************************/
static void uart_command_task(void *arg)
{
    uint8_t data[BUF_SIZE];

    while (1)
    {
        // 从UART读取数据，等待1000ms
        int len =
            uart_read_bytes(EX_UART_NUM, data, BUF_SIZE - 1, pdMS_TO_TICKS(1000));
        if (len > 0)
        {
            data[len] = '\0'; // 字符串结束符

            char cmd[32], arg1[64], arg2[64];
            int parsed = sscanf((char *)data, "%s %63s %63s", cmd, arg1, arg2);

            if (parsed >= 1)
            {
                // help命令：打印所有支持的命令
                if (strcmp(cmd, "help") == 0)
                {
                    printf("Commands:\n");
                    printf("  set_sta <ssid> <pass>  - configure STA mode\n");
                    printf("  set_ap <ssid> <pass>   - configure AP mode\n");
                    printf("  mode ap                - switch to AP mode\n");
                    printf("  mode sta               - switch to STA mode\n");
                    printf("  mode mix               - switch to AP+STA mode\n");
                    printf("  set_tcp_port <port>    - set TCP server port (restart "
                           "server)\n");
                    printf("  info                   - show current WiFi mode and TCP "
                           "port\n");
                }
                // 配置STA的SSID和密码
                else if (strcmp(cmd, "set_sta") == 0 && parsed == 3)
                {
                    strncpy((char *)sta_config.sta.ssid, arg1,
                            sizeof(sta_config.sta.ssid));
                    strncpy((char *)sta_config.sta.password, arg2,
                            sizeof(sta_config.sta.password));
                    printf("STA configured: SSID=%s\n", arg1);

                    if (current_mode & WIFI_MODE_STA)
                    {
                        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
                        esp_wifi_connect();
                    }
                }
                // 配置AP的SSID和密码
                else if (strcmp(cmd, "set_ap") == 0 && parsed == 3)
                {
                    strncpy((char *)ap_config.ap.ssid, arg1, sizeof(ap_config.ap.ssid));
                    strncpy((char *)ap_config.ap.password, arg2,
                            sizeof(ap_config.ap.password));
                    ap_config.ap.max_connection = 4;
                    ap_config.ap.authmode =
                        strlen(arg2) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
                    printf("AP configured: SSID=%s\n", arg1);

                    if (current_mode & WIFI_MODE_AP)
                    {
                        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
                    }
                }
                // 切换WiFi模式
                else if (strcmp(cmd, "mode") == 0 && parsed >= 2)
                {
                    if (strcmp(arg1, "ap") == 0)
                    {
                        start_wifi_mode(WIFI_MODE_AP);
                    }
                    else if (strcmp(arg1, "sta") == 0)
                    {
                        start_wifi_mode(WIFI_MODE_STA);
                    }
                    else if (strcmp(arg1, "mix") == 0)
                    {
                        start_wifi_mode(WIFI_MODE_APSTA);
                    }
                    else
                    {
                        printf("Unknown mode: %s\n", arg1);
                    }
                }
                // 动态设置TCP端口号（重启TCP服务器生效）
                else if (strcmp(cmd, "set_tcp_port") == 0 && parsed == 2)
                {
                    int new_port = atoi(arg1);
                    if (new_port > 0 && new_port < 65536)
                    {
                        if (tcp_port != new_port)
                        {
                            printf("Changing TCP port from %d to %d\n", tcp_port, new_port);
                            tcp_port = new_port;
                            tcp_server_stop();
                            tcp_server_start();
                        }
                        else
                        {
                            printf("TCP port already set to %d\n", tcp_port);
                        }
                    }
                    else
                    {
                        printf("Invalid port number: %s\n", arg1);
                    }
                }
                // 查看当前状态信息
                else if (strcmp(cmd, "info") == 0)
                {
                    printf("Current WiFi mode: %s%s\n",
                           (current_mode & WIFI_MODE_AP) ? "AP " : "",
                           (current_mode & WIFI_MODE_STA) ? "STA" : "");
                    printf("TCP server port: %d\n", tcp_port);
                }
                else
                {
                    printf("Unknown command or bad arguments: %s\n", cmd);
                }
            }
        }
    }
}

/************************** WiFi与UART初始化 **************************/
void wifi_init_and_start(void)
{
    // 初始化NVS（非易失存储，用于WiFi配置等）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 创建事件组用于WiFi连接状态管理
    wifi_event_group = xEventGroupCreate();

    // 初始化TCP/IP适配器及事件循环
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 创建默认AP和STA网络接口
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    // 初始化WiFi驱动，使用默认配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册WiFi事件处理回调
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 默认AP配置
    strncpy((char *)ap_config.ap.ssid, "MyAP", sizeof(ap_config.ap.ssid));
    strncpy((char *)ap_config.ap.password, "12345678",
            sizeof(ap_config.ap.password));
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    // 清空STA配置
    memset(&sta_config, 0, sizeof(sta_config));

    // 启动默认AP+STA共存模式
    start_wifi_mode(WIFI_MODE_APSTA);

    // 启动TCP服务器任务
    tcp_server_start();

    // UART初始化参数设置
    uart_config_t uart_config = {.baud_rate = UART_BAUD_RATE,
                                 .data_bits = UART_DATA_8_BITS,
                                 .parity = UART_PARITY_DISABLE,
                                 .stop_bits = UART_STOP_BITS_1,
                                 .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

    uart_param_config(EX_UART_NUM, &uart_config);
    // uart_set_pin(EX_UART_NUM, UART_TXD, UART_RXD, UART_PIN_NO_CHANGE,
    // UART_PIN_NO_CHANGE);
    uart_driver_install(EX_UART_NUM, 1024, 0, 0, NULL, 0);
}

/************************** Wifi接收队列任务处理 **************************/
void wifi_rxdata_handler_task(void *pvParameters)
{
    wifi_module_queue_init(); // 初始化队列

    char *rx_msg = NULL;

    while (1)
    {
        if (xQueueReceive(wifi_rx_queue, &rx_msg, portMAX_DELAY) == pdPASS)
        {
            // rx_msg[BUF_SIZE - 1] = '\0'; // 确保字符串结束符

            char cmd[32], arg1[64], arg2[64];
            int parsed = sscanf((char *)rx_msg, "%s %63s %63s", cmd, arg1, arg2);

            if (parsed >= 1)
            {
                // help命令：打印所有支持的命令
                if (strcmp(cmd, "help") == 0)
                {
                    const char *help_msg =
                        "Commands:\n"
                        "  set_sta <ssid> <pass>  - configure STA mode\n"
                        "  set_ap <ssid> <pass>   - configure AP mode\n"
                        "  mode ap                - switch to AP mode\n"
                        "  mode sta               - switch to STA mode\n"
                        "  mode mix               - switch to AP+STA mode\n"
                        "  set_tcp_port <port>    - set TCP server port (restart "
                        "server)\n"
                        "  info                   - show current WiFi mode and TCP "
                        "port\n";

                    char *data = malloc(strlen(help_msg) + 1);
                    if (data)
                    {
                        strcpy(data, help_msg);
                        xQueueSend(wifi_tx_queue, &data, portMAX_DELAY);
                    }
                }

                // 配置STA的SSID和密码
                else if (strcmp(cmd, "set_sta") == 0 && parsed == 3)
                {
                    strncpy((char *)sta_config.sta.ssid, arg1,
                            sizeof(sta_config.sta.ssid));
                    strncpy((char *)sta_config.sta.password, arg2,
                            sizeof(sta_config.sta.password));
                    mywifi_log("STA configured: SSID=%s\n", arg1);

                    if (current_mode & WIFI_MODE_STA)
                    {
                        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
                        esp_wifi_connect();
                    }
                }
                // 配置AP的SSID和密码
                else if (strcmp(cmd, "set_ap") == 0 && parsed == 3)
                {
                    // mywifi_log("set_ap %s to %s\n", arg1, arg2);
                    // tcp_server_stop();
                    // tcp_server_start();
                    strncpy((char *)ap_config.ap.ssid, arg1, sizeof(ap_config.ap.ssid));
                    strncpy((char *)ap_config.ap.password, arg2,
                            sizeof(ap_config.ap.password));
                    ap_config.ap.max_connection = 4;
                    ap_config.ap.authmode =
                        strlen(arg2) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
                    mywifi_log("AP configured: SSID=%s\n", arg1);

                    if (current_mode & WIFI_MODE_AP)
                    {
                        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
                    }
                }
                // 切换WiFi模式
                else if (strcmp(cmd, "mode") == 0 && parsed >= 2)
                {
                    if (strcmp(arg1, "ap") == 0)
                    {
                        start_wifi_mode(WIFI_MODE_AP);
                    }
                    else if (strcmp(arg1, "sta") == 0)
                    {
                        start_wifi_mode(WIFI_MODE_STA);
                    }
                    else if (strcmp(arg1, "mix") == 0)
                    {
                        start_wifi_mode(WIFI_MODE_APSTA);
                    }
                    else
                    {
                        mywifi_log("Unknown mode: %s\n", arg1);
                    }
                }
                // 动态设置TCP端口号（重启TCP服务器生效）
                else if (strcmp(cmd, "set_tcp_port") == 0 && parsed == 2)
                {
                    int new_port = atoi(arg1);
                    if (new_port > 0 && new_port < 65536)
                    {
                        if (tcp_port != new_port)
                        {
                            mywifi_log("Changing TCP port from %d to %d\n", tcp_port,
                                       new_port);
                            tcp_port = new_port;
                            tcp_server_stop();
                            tcp_server_start();
                        }
                        else
                        {
                            mywifi_log("TCP port already set to %d\n", tcp_port);
                        }
                    }
                    else
                    {
                        mywifi_log("Invalid port number: %s\n", arg1);
                    }
                }
                // 查看当前状态信息
                else if (strcmp(cmd, "info") == 0)
                {
                    char mode_str[16] = {0};
                    if (current_mode & WIFI_MODE_AP)
                        strcat(mode_str, "AP ");
                    if (current_mode & WIFI_MODE_STA)
                        strcat(mode_str, "STA");

                    // 直接调用 mywifi_log 格式化输出，避免中间缓冲区
                    mywifi_log("Info:\n"
                               "  WiFi mode       : %s\n"
                               "  TCP server port : %d\n",
                               mode_str, tcp_port);
                }
                else
                {
                    mywifi_log("Unknown command or bad arguments: %s\n", cmd);
                }
            }
        }
        free(rx_msg); // 释放接收到的消息内存
    }
}

/************************** WiFi发送队列任务处理 **************************/
void wifi_txdata_handler_task(void *pvParameters)
{
    while (1)
    {
        char *tx_msg = NULL;
        if (xQueueReceive(wifi_tx_queue, &tx_msg, portMAX_DELAY))
        {
            // 发送数据到串口

            // 释放发送的数据
            free(tx_msg);
        }
    }
}

/************************** WiFi模块队列初始化 **************************/
void wifi_module_queue_init(void)
{
    // 创建WiFi发送和接收队列
    wifi_tx_queue = xQueueCreate(128, sizeof(char *)); // 发送队列
    wifi_rx_queue = xQueueCreate(128, sizeof(char *)); // 接收队列

    if (wifi_tx_queue == NULL || wifi_rx_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi queues");
        return;
    }
    ESP_LOGI(TAG, "WiFi queues created successfully");
}

/************************** WiFi日志打印函数 **************************/
void mywifi_log(const char *fmt, ...)
{
    // 分配缓冲区用于格式化日志
    char *log_buf = malloc(BUF_SIZE);
    if (!log_buf)
        return;

    va_list args;
    va_start(args, fmt);
    vsnprintf(log_buf, BUF_SIZE, fmt, args);
    va_end(args);

    // 打印到串口
    printf("%s", log_buf);

    // 直接将log_buf指针入队，队列任务负责释放
    if (wifi_tx_queue != NULL)
    {
        // 尽量避免阻塞，超时设0
        if (xQueueSend(wifi_tx_queue, &log_buf, pdMS_TO_TICKS(10)) != pdTRUE)
        {
            // 如果发送失败，直接释放，避免内存泄漏
            free(log_buf);
        }
    }
    else
    {
        // 队列不存在直接释放
        free(log_buf);
    }
}

/************************** 程序入口 **************************/
void Wifi_task(void)
{
    // 初始化WiFi和UART
    wifi_init_and_start();
    // 创建串口命令任务
    // xTaskCreate(uart_command_task, "uart_cmd_task", 4096, NULL, 5, NULL);
    // 创建WiFi数据处理任务
    xTaskCreate(wifi_rxdata_handler_task, "wifi_data_handler_task", 4096, NULL, 5,
                NULL);
}
