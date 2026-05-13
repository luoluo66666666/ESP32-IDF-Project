#ifndef __TEMP_H__
#define __TEMP_H__

#include <stdint.h>

#include "esp_err.h"

/* 按旧流程设置恒温器目标温度 */
esp_err_t temp_set_target_temperature(uint16_t temperature);

/* 按旧流程读取恒温器状态 */
esp_err_t temp_read_status(uint16_t head[2], uint16_t regs[4]);

/* 读取当前设定温度 */
esp_err_t temp_read_target_temperature(uint16_t *temperature);

/* 读取当前出水温度 */
esp_err_t temp_read_water_temperature(uint16_t *temperature);

/* 读取当前出水流量 */
esp_err_t temp_read_water_flow(uint16_t *flow);

/* 读取当前出水温度和流量 */
esp_err_t temp_read_water_temp_flow(uint16_t regs[3]);

/* 初始化恒温器默认目标温度 */
esp_err_t temp_init_default_target(void);

/* 恒温器测试任务 */
void temp_test_task(void *arg);

#endif

