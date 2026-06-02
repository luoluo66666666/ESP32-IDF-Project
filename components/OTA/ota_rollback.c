#include "ota_rollback.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "Ota_rollback";

#ifndef CONFIG_APP_ROLLBACK_ENABLE
#define OTA_ROLLBACK_SUPPORTED 0
#else
#define OTA_ROLLBACK_SUPPORTED 1
#endif

/* 新固件启动后稳定运行多久再确认有效（秒） */
#define OTA_ROLLBACK_CONFIRM_DELAY_SEC 15

static const char *ota_state_name(esp_ota_img_states_t state)
{
    switch (state)
    {
    case ESP_OTA_IMG_NEW:
        return "NEW";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "PENDING_VERIFY";
    case ESP_OTA_IMG_VALID:
        return "VALID";
    case ESP_OTA_IMG_INVALID:
        return "INVALID";
    case ESP_OTA_IMG_ABORTED:
        return "ABORTED";
    case ESP_OTA_IMG_UNDEFINED:
    default:
        return "UNDEFINED";
    }
}

static void confirm_running_firmware_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(OTA_ROLLBACK_CONFIRM_DELAY_SEC * 1000));

#if OTA_ROLLBACK_SUPPORTED
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;

    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK)
    {
        ESP_LOGW(TAG, "Skip confirm: cannot read OTA state");
        vTaskDelete(NULL);
        return;
    }

    if (state != ESP_OTA_IMG_PENDING_VERIFY)
    {
        ESP_LOGI(TAG, "Skip confirm: partition %s state=%s", running->label, ota_state_name(state));
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "New firmware confirmed valid after %ds stable run", OTA_ROLLBACK_CONFIRM_DELAY_SEC);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to confirm firmware: %s", esp_err_to_name(err));
    }
#else
    ESP_LOGW(TAG, "APP rollback disabled in sdkconfig; confirm skipped");
#endif

    vTaskDelete(NULL);
}

void ota_rollback_init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();

    if (running == NULL)
    {
        ESP_LOGW(TAG, "No running app partition");
        return;
    }

    ESP_LOGI(
        TAG,
        "Running app: %s @ 0x%lx, boot slot: %s",
        running->label,
        (unsigned long)running->address,
        boot != NULL ? boot->label : "unknown");

#if OTA_ROLLBACK_SUPPORTED
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK)
    {
        ESP_LOGI(TAG, "OTA image state: %s", ota_state_name(state));
        if (state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            ESP_LOGW(
                TAG,
                "New firmware pending verify; auto-rollback if not confirmed within %ds stable run",
                OTA_ROLLBACK_CONFIRM_DELAY_SEC);
        }
        else if (state == ESP_OTA_IMG_ABORTED)
        {
            ESP_LOGW(TAG, "Previous OTA was aborted; now running rolled-back firmware");
        }
    }

    if (esp_ota_check_rollback_is_possible())
    {
        ESP_LOGI(TAG, "Bootloader rollback is enabled");
    }
    else
    {
        ESP_LOGW(TAG, "Bootloader rollback not available (check partition table / otadata)");
    }
#else
    ESP_LOGW(TAG, "CONFIG_APP_ROLLBACK_ENABLE is off; enable it for auto rollback");
#endif
}

void ota_rollback_schedule_confirm(void)
{
    BaseType_t ok = xTaskCreate(
        confirm_running_firmware_task,
        "ota_confirm",
        3072,
        NULL,
        5,
        NULL);

    if (ok != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create OTA confirm task");
    }
}
