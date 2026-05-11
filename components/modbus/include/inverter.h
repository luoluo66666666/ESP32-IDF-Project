#ifndef __INVERTER_H__
#define __INVERTER_H__

#include <stdint.h>

#include "esp_err.h"

/* 启动变频器 */
esp_err_t inverter_start(void);

/* 停止变频器 */
esp_err_t inverter_stop(void);

/* 设置变频器压力，单位 0.1 */
esp_err_t inverter_set_pressure_tenths(uint16_t pressure_x10);

/* 设置变频器压力 */
esp_err_t inverter_set_pressure(float pressure);

/* 读取变频器原始压力值 */
esp_err_t inverter_read_pressure_raw(uint16_t *pressure_raw);

/* 读取变频器压力 */
esp_err_t inverter_read_pressure(float *pressure);

/* 变频器测试任务 */
void inverter_test_task(void *arg);

#endif
