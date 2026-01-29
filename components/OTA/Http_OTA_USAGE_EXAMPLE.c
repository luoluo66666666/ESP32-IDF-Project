/*
 * Http_OTA 使用示例
 *
 * 将以下代码添加到你的 main.c 中
 */

#include "Http_ota.h"

// 全局句柄
static TaskHandle_t g_http_ota_task_handle = NULL;

/**
 * 初始化 HTTP OTA 任务
 * 在 app_main 中调用，确保 NVS 和 WiFi 已初始化
 */
void app_http_ota_init(void)
{
    // 配置 HTTP OTA 参数
    http_ota_config_t cfg = {
        .firmware_url = "http://192.168.1.100/firmware.bin",  // 默认固件 URL
        .task_stack_size = 8192,                                // 栈大小（8KB）
        .task_prio = 5,                                         // 优先级
    };

    // 启动 HTTP OTA 任务
    g_http_ota_task_handle = http_ota_start(&cfg);
    if (g_http_ota_task_handle) {
        ESP_LOGI("app", "HTTP OTA 任务启动成功");
    } else {
        ESP_LOGE("app", "HTTP OTA 任务启动失败");
    }
}

/**
 * 触发 HTTP OTA 升级（例如通过蓝牙、REST API 等接收到的命令）
 *
 * 使用示例：
 *  - 使用默认 URL：http_ota_trigger(NULL);
 *  - 使用临时 URL：http_ota_trigger("http://example.com/new.bin");
 */
void app_http_ota_trigger_upgrade(const char *url)
{
    esp_err_t ret = http_ota_trigger(url);
    if (ret == ESP_OK) {
        ESP_LOGI("app", "HTTP OTA 升级已触发");
    } else {
        ESP_LOGE("app", "HTTP OTA 升级触发失败：%s", esp_err_to_name(ret));
    }
}

/**
 * 停止 HTTP OTA 任务（资源清理）
 */
void app_http_ota_deinit(void)
{
    if (g_http_ota_task_handle) {
        http_ota_stop(g_http_ota_task_handle);
        g_http_ota_task_handle = NULL;
    }
}

/*
 * 在 app_main 中的使用流程：
 *
 * void app_main(void)
 * {
 *     // ... 初始化 NVS ...
 *     nvs_flash_init();
 *
 *     // ... 初始化 WiFi ...
 *     wifi_init_sta();
 *
 *     // 初始化 HTTP OTA 任务
 *     app_http_ota_init();
 *
 *     // ... 你的其他业务代码 ...
 *
 *     // 当需要 HTTP OTA 时，调用：
 *     // app_http_ota_trigger_upgrade(NULL);
 *
 *     // 应用退出前，清理资源：
 *     // app_http_ota_deinit();
 * }
 */

/**
 * 封装函数：在内部完成 NVS 初始化、可选 WiFi 初始化，并启动 HTTP OTA 任务
 * @param default_url 默认固件 URL（可为 NULL）
 * @param wifi_init_cb 可选的 WiFi 初始化回调（如果为 NULL，假定外部已初始化 WiFi）
 *
 * 示例：
 *     app_http_ota_start_with_init("http://192.168.1.100/firmware.bin", wifi_init_sta);
 */
void app_http_ota_start_with_init(const char *default_url, void (*wifi_init_cb)(void))
{
    // 初始化 NVS（处理可能的旧版本或空间不足情况）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 可选 WiFi 初始化回调
    if (wifi_init_cb) {
        wifi_init_cb();
    }

    // 启动 HTTP OTA 任务
    http_ota_config_t cfg = {0};
    if (default_url) {
        strncpy(cfg.firmware_url, default_url, sizeof(cfg.firmware_url) - 1);
    }
    cfg.task_stack_size = 8192;
    cfg.task_prio = 5;

    g_http_ota_task_handle = http_ota_start(&cfg);
}

