#include "temp.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_master.h"

#define TAG "TEMP"
#define TEMP_SLAVE_ADDR 0x01
#define TEMP_ENABLE_REG 0x0000
#define TEMP_SET_REG 0x0001
#define TEMP_ENABLE_VALUE 0x00C0
#define TEMP_TARGET_READ_REG 0x0001
#define TEMP_WATER_TEMP_REG 0x0002
#define TEMP_WATER_FLOW_REG 0x0003
#define TEMP_DEFAULT_TARGET 39
#define TEMP_INIT_RETRY_COUNT 3

/* 设置恒温器目标温度 */
esp_err_t temp_set_target_temperature(uint16_t temperature)
{
    esp_err_t err;
    uint16_t reg = 0;
    uint16_t regs[4] = {0};
    uint16_t regs_head[2] = {0};

    err = modbus_write_single_register(TEMP_SLAVE_ADDR, TEMP_ENABLE_REG, TEMP_ENABLE_VALUE);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = modbus_write_single_register(TEMP_SLAVE_ADDR, TEMP_SET_REG, temperature);
    }
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_ENABLE_REG, 2, regs_head);
    }
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = temp_read_target_temperature(&reg);
    }
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_SET_REG, 4, regs);
    }
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    return err;
}

/* 读取恒温器状态 */
esp_err_t temp_read_status(uint16_t head[2], uint16_t regs[4])
{
    esp_err_t err;

    if (head == NULL || regs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_ENABLE_REG, 2, head);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_SET_REG, 4, regs);
    }

    return err;
}

/* 读取当前设定温度 */
esp_err_t temp_read_target_temperature(uint16_t *temperature)
{
    return modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_TARGET_READ_REG, 1, temperature);
}

/* 读取当前出水温度 */
esp_err_t temp_read_water_temperature(uint16_t *temperature)
{
    return modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_WATER_TEMP_REG, 1, temperature);
}

/* 读取当前出水流量 */
esp_err_t temp_read_water_flow(uint16_t *flow)
{
    return modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_WATER_FLOW_REG, 1, flow);
}

/* 读取当前出水温度和流量 */
esp_err_t temp_read_water_temp_flow(uint16_t regs[3])
{
    return modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_WATER_TEMP_REG, 3, regs);
}

/* 初始化恒温器默认目标温度 */
esp_err_t temp_init_default_target(void)
{
    esp_err_t err = ESP_FAIL;
    uint16_t target = 0;

    vTaskDelay(pdMS_TO_TICKS(500));

    for (int i = 0; i < TEMP_INIT_RETRY_COUNT; i++) {
        err = temp_set_target_temperature(TEMP_DEFAULT_TARGET);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "default target set failed, retry=%d err=%s",
                     i + 1, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
        err = temp_read_target_temperature(&target);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "default target initialized: set=%u readback=%u",
                     TEMP_DEFAULT_TARGET, target);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "default target readback failed, retry=%d err=%s",
                 i + 1, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    ESP_LOGE(TAG, "default target init failed after retries");
    return err;
}

/* 恒温器测试任务 */
void temp_test_task(void *arg)
{
    uint16_t target = 0;
    uint16_t water_temp = 0;
    uint16_t flow = 0;

    while (1) {
        esp_err_t err = temp_set_target_temperature(39);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "set target temperature 39 ok");
        } else {
            ESP_LOGE(TAG, "set target temperature failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));

        err = temp_read_target_temperature(&target);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "target temperature = %u", target);
        } else {
            ESP_LOGE(TAG, "read target temperature failed: %s", esp_err_to_name(err));
        }

        err = temp_read_water_temperature(&water_temp);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "water temperature = %u", water_temp);
        } else {
            ESP_LOGE(TAG, "read water temperature failed: %s", esp_err_to_name(err));
        }

        err = temp_read_water_flow(&flow);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "water flow = %u", flow);
        } else {
            ESP_LOGE(TAG, "read water flow failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
