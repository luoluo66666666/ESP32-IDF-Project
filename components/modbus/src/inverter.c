#include "inverter.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_master.h"

#define TAG "INVERTER"
#define INVERTER_SLAVE_ADDR 0x03
#define INVERTER_RUN_REG 0x2000
#define INVERTER_PRESSURE_SET_REG 0xF000
#define INVERTER_PRESSURE_READ_REG 0x100D

/* 启动变频器 */
esp_err_t inverter_start(void)
{
    return modbus_write_single_register(INVERTER_SLAVE_ADDR, INVERTER_RUN_REG, 0x0001);
}

/* 停止变频器 */
esp_err_t inverter_stop(void)
{
    return modbus_write_single_register(INVERTER_SLAVE_ADDR, INVERTER_RUN_REG, 0x0005);
}

/* 设置变频器压力，单位 0.1 */
esp_err_t inverter_set_pressure_tenths(uint16_t pressure_x10)
{
    return modbus_write_single_register(INVERTER_SLAVE_ADDR, INVERTER_PRESSURE_SET_REG, pressure_x10);
}

/* 设置变频器压力 */
esp_err_t inverter_set_pressure(float pressure)
{
    if (pressure < 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    return inverter_set_pressure_tenths((uint16_t)(pressure * 10.0f + 0.5f));
}

/* 读取变频器原始压力值 */
esp_err_t inverter_read_pressure_raw(uint16_t *pressure_raw)
{
    return modbus_read_holding_registers(INVERTER_SLAVE_ADDR, INVERTER_PRESSURE_READ_REG, 1, pressure_raw);
}

/* 读取变频器压力 */
esp_err_t inverter_read_pressure(float *pressure)
{
    if (pressure == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw = 0;
    esp_err_t err = inverter_read_pressure_raw(&raw);
    if (err != ESP_OK) {
        return err;
    }

    *pressure = ((float)raw) / 10.0f;
    return ESP_OK;
}

/* 变频器测试任务 */
void inverter_test_task(void *arg)
{
    float pressure = 0.0f;

    while (1) {
        esp_err_t err = inverter_set_pressure(5.0f);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "set pressure 5.0 ok");
        } else {
            ESP_LOGE(TAG, "set pressure failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

        err = inverter_start();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "start ok");
        } else {
            ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));

        err = inverter_read_pressure(&pressure);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "pressure = %.1f", pressure);
        } else {
            ESP_LOGE(TAG, "read pressure failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));

        err = inverter_stop();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "stop ok");
        } else {
            ESP_LOGE(TAG, "stop failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
