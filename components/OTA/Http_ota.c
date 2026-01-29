/*
 * Http_ota.c
 *
 * 一个独立、稳健的 HTTP(S) OTA 后台任务实现：
 * - 支持传入临时 URL（通过互斥保护的缓冲区）
 * - 使用信号量触发 OTA 执行
 * - 依赖 esp_https_ota + esp_http_client + app_update
 * - 成功后自动重启，失败时尝试回滚到当前分区
 */

#include "Http_ota.h"
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/semphr.h"

static const char *TAG = "Http_ota";

static http_ota_config_t s_cfg = {0};
static TaskHandle_t s_task_handle = NULL;
static SemaphoreHandle_t s_trigger_sem = NULL;

// 临时 URL 缓冲与互斥锁（保护外部传入临时 URL）
static char s_temp_url[256] = {0};
static SemaphoreHandle_t s_temp_url_mutex = NULL;

/* HTTP OTA 后台任务实现 */
static void http_ota_task_main(void *arg)
{
    ESP_LOGI(TAG, "========== HTTP OTA Task Started! ==========");

    for (;;) {
        if (xSemaphoreTake(s_trigger_sem, portMAX_DELAY) == pdTRUE) {
            char url_local[256] = {0};

            // Try to get temporary URL first
            if (s_temp_url_mutex && xSemaphoreTake(s_temp_url_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                if (s_temp_url[0] != '\0') {
                    strncpy(url_local, s_temp_url, sizeof(url_local) - 1);
                    s_temp_url[0] = '\0'; // Clear after use
                }
                xSemaphoreGive(s_temp_url_mutex);
            }

            // If no temporary URL, use default config URL
            if (url_local[0] == '\0') {
                strncpy(url_local, s_cfg.firmware_url, sizeof(url_local) - 1);
            }

            if (strlen(url_local) == 0) {
                ESP_LOGE(TAG, "No valid firmware URL, skip this OTA");
                continue;
            }

            ESP_LOGI(TAG, "Starting HTTP OTA: %s", url_local);

            // Configure HTTP client
            esp_http_client_config_t http_cfg = {
                .url = url_local,
                .timeout_ms = 15000,
                .skip_cert_common_name_check = true,
            };

            // Configure OTA (v5.4.2 API, pass HTTP config pointer)
            esp_https_ota_config_t ota_cfg = {
                .http_config = &http_cfg,
            };

            // Execute OTA upgrade
            esp_err_t err = esp_https_ota(&ota_cfg);

            if (err == ESP_OK) {
                ESP_LOGI(TAG, "HTTP OTA success, restart in 2 seconds");
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_restart();
            } else {
                ESP_LOGE(TAG, "HTTP OTA failed: %s", esp_err_to_name(err));
                const esp_partition_t *running = esp_ota_get_running_partition();
                if (running) {
                    esp_ota_set_boot_partition(running);
                }
            }

            // Prevent immediate frequent triggering
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

/* API: Start HTTP OTA task */
TaskHandle_t http_ota_start(const http_ota_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Start failed: config is NULL");
        return NULL;
    }

    // Save config
    memset(&s_cfg, 0, sizeof(s_cfg));
    strncpy(s_cfg.firmware_url, config->firmware_url, sizeof(s_cfg.firmware_url) - 1);
    s_cfg.task_stack_size = config->task_stack_size ? config->task_stack_size : 4096;
    s_cfg.task_prio = config->task_prio ? config->task_prio : 5;

    // Create trigger semaphore
    s_trigger_sem = xSemaphoreCreateBinary();
    if (!s_trigger_sem) {
        ESP_LOGE(TAG, "Failed to create trigger semaphore");
        return NULL;
    }

    // Create temporary URL mutex
    s_temp_url_mutex = xSemaphoreCreateMutex();
    if (!s_temp_url_mutex) {
        ESP_LOGE(TAG, "Failed to create URL mutex");
        vSemaphoreDelete(s_trigger_sem);
        s_trigger_sem = NULL;
        return NULL;
    }

    // Create task
    BaseType_t r = xTaskCreate(http_ota_task_main, "http_ota_task", s_cfg.task_stack_size / sizeof(StackType_t), NULL, s_cfg.task_prio, &s_task_handle);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "Failed to create HTTP OTA task");
        vSemaphoreDelete(s_trigger_sem);
        s_trigger_sem = NULL;
        vSemaphoreDelete(s_temp_url_mutex);
        s_temp_url_mutex = NULL;
        return NULL;
    }

    ESP_LOGI(TAG, "HTTP OTA task started successfully");
    return s_task_handle;
}

/* API: Stop HTTP OTA task */
void http_ota_stop(TaskHandle_t task_handle)
{
    if (task_handle == NULL) {
        ESP_LOGW(TAG, "Stop HTTP OTA: handle is NULL");
        return;
    }

    vTaskDelete(task_handle);
    s_task_handle = NULL;

    if (s_trigger_sem) {
        vSemaphoreDelete(s_trigger_sem);
        s_trigger_sem = NULL;
    }
    if (s_temp_url_mutex) {
        vSemaphoreDelete(s_temp_url_mutex);
        s_temp_url_mutex = NULL;
    }

    s_temp_url[0] = '\0';
    ESP_LOGI(TAG, "HTTP OTA task stopped and resources freed");
}

/* API: Trigger HTTP OTA (optional temporary URL) */
esp_err_t http_ota_trigger(const char *firmware_url)
{
    if (!s_trigger_sem) {
        ESP_LOGE(TAG, "Trigger failed: HTTP OTA task not started");
        return ESP_ERR_INVALID_STATE;
    }

    if (firmware_url && strlen(firmware_url) > 0) {
        if (s_temp_url_mutex && xSemaphoreTake(s_temp_url_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            strncpy(s_temp_url, firmware_url, sizeof(s_temp_url) - 1);
            xSemaphoreGive(s_temp_url_mutex);
            ESP_LOGI(TAG, "Temporary firmware URL set: %s", firmware_url);
        } else {
            ESP_LOGW(TAG, "Failed to acquire URL mutex, using default URL");
        }
    } else {
        ESP_LOGI(TAG, "Using default firmware URL from config");
    }

    xSemaphoreGive(s_trigger_sem);
    return ESP_OK;
}
