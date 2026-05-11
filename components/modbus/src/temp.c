#include "temp.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_master.h"

#define TAG "TEMP"
#define TEMP_SLAVE_ADDR 0x01
#define TEMP_TARGET_SET_REG 0x000C
#define TEMP_TARGET_READ_REG 0x0002
#define TEMP_WATER_TEMP_REG 0x0001
#define TEMP_WATER_FLOW_REG 0x0003

/* 设置恒温宝目标温度 */
esp_err_t temp_set_target_temperature(uint16_t temperature)
{
    return modbus_write_single_register(TEMP_SLAVE_ADDR, TEMP_TARGET_SET_REG, temperature);
}

/* 读取恒温宝目标温度 */
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

/* 恒温宝测试任务 */
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
