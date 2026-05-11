#ifndef __TEMP_H__
#define __TEMP_H__

#include <stdint.h>

#include "esp_err.h"

/* 设置恒温宝目标温度 */
esp_err_t temp_set_target_temperature(uint16_t temperature);

/* 读取恒温宝目标温度 */
esp_err_t temp_read_target_temperature(uint16_t *temperature);

/* 读取当前出水温度 */
esp_err_t temp_read_water_temperature(uint16_t *temperature);

/* 读取当前出水流量 */
esp_err_t temp_read_water_flow(uint16_t *flow);

/* 读取当前出水温度和流量 */
esp_err_t temp_read_water_temp_flow(uint16_t regs[3]);

/* 恒温宝测试任务 */
void temp_test_task(void *arg);

#endif
