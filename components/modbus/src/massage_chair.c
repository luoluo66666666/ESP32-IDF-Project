#include "massage_chair.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_master.h"

#define TAG "MASSAGE_CHAIR"
#define MASSAGE_CHAIR_SLAVE_ADDR 0x06
#define MASSAGE_MODE_REG 0x0001
#define MASSAGE_STRENGTH_REG 0x0002

/* 停止按摩椅 */
esp_err_t massage_chair_stop(void)
{
    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_MODE_REG, 0x0000);
}

/* 设置按摩椅模式 */
esp_err_t massage_chair_set_mode(massage_mode_t mode)
{
    if (mode > MASSAGE_MODE_3) {
        return ESP_ERR_INVALID_ARG;
    }

    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_MODE_REG, (uint16_t)mode);
}

/* 设置按摩椅低力度 */
esp_err_t massage_chair_set_strength_low(void)
{
    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_MODE_REG, 0x0002);
}

/* 设置按摩椅中力度 */
esp_err_t massage_chair_set_strength_medium(void)
{
    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_STRENGTH_REG, 0x0001);
}

/* 设置按摩椅高力度 */
esp_err_t massage_chair_set_strength_high(void)
{
    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_STRENGTH_REG, 0x0002);
}

/* 按摩椅测试任务 */
void massage_chair_test_task(void *arg)
{
    while (1) {
        esp_err_t err = massage_chair_set_mode(MASSAGE_MODE_1);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "set mode 1 ok");
        } else {
            ESP_LOGE(TAG, "set mode 1 failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));

        err = massage_chair_set_strength_medium();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "set medium strength ok");
        } else {
            ESP_LOGE(TAG, "set medium strength failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));

        err = massage_chair_stop();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "stop ok");
        } else {
            ESP_LOGE(TAG, "stop failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
