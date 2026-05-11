#ifndef __MODBUS_H__
#define __MODBUS_H__

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    MODBUS_FC_READ_COILS = 0x01,
    MODBUS_FC_READ_DISCRETE_INPUTS = 0x02,
    MODBUS_FC_READ_HOLDING_REGISTERS = 0x03,
    MODBUS_FC_READ_INPUT_REGISTERS = 0x04,
    MODBUS_FC_WRITE_SINGLE_COIL = 0x05,
    MODBUS_FC_WRITE_SINGLE_REGISTER = 0x06,
    MODBUS_FC_WRITE_MULTIPLE_COILS = 0x0F,
    MODBUS_FC_WRITE_MULTIPLE_REGISTERS = 0x10
} modbus_function_code_t;

/* 按摩椅模式 */
typedef enum {
    MASSAGE_MODE_STOP = 0,
    MASSAGE_MODE_1 = 1,
    MASSAGE_MODE_2 = 2,
    MASSAGE_MODE_3 = 3
} massage_mode_t;

/* 初始化 Modbus 主站 */
void modbus_init(void);
/* 查询 Modbus 是否已初始化 */
bool modbus_is_initialized(void);

/* 读取保持寄存器 */
esp_err_t modbus_read_holding_registers(uint8_t slave_addr, uint16_t reg_start,
                                        uint16_t reg_count, uint16_t *buffer);
/* 写单个寄存器 */
esp_err_t modbus_write_single_register(uint8_t slave_addr, uint16_t reg_addr, uint16_t value);
/* 写多个寄存器 */
esp_err_t modbus_write_multiple_registers(uint8_t slave_addr, uint16_t reg_start,
                                          const uint16_t *buffer, uint16_t reg_count);

/* Modbus 通讯测试 */
bool modbus_comm_test(uint8_t slave_addr, uint16_t reg_start, uint16_t reg_count);
/* Modbus 测试任务 */
void modbus_test_task(void *arg);

/* 按摩椅停止 */
esp_err_t massage_chair_stop(void);
/* 设置按摩椅模式 */
esp_err_t massage_chair_set_mode(massage_mode_t mode);
/* 设置按摩椅低力度 */
esp_err_t massage_chair_set_strength_low(void);
/* 设置按摩椅中力度 */
esp_err_t massage_chair_set_strength_medium(void);
/* 设置按摩椅高力度 */
esp_err_t massage_chair_set_strength_high(void);

/* 变频器启动 */
esp_err_t inverter_start(void);
/* 变频器停止 */
esp_err_t inverter_stop(void);
/* 设置变频器压力，单位 0.1 */
esp_err_t inverter_set_pressure_tenths(uint16_t pressure_x10);
/* 设置变频器压力 */
esp_err_t inverter_set_pressure(float pressure);
/* 读取变频器原始压力值 */
esp_err_t inverter_read_pressure_raw(uint16_t *pressure_raw);
/* 读取变频器压力 */
esp_err_t inverter_read_pressure(float *pressure);

/* 设置恒温室目标温度 */
esp_err_t thermostat_set_target_temperature(uint16_t temperature);
/* 读取恒温室目标温度 */
esp_err_t thermostat_read_target_temperature(uint16_t *temperature);
/* 读取当前出水温度 */
esp_err_t thermostat_read_water_temperature(uint16_t *temperature);
/* 读取当前出水流量 */
esp_err_t thermostat_read_water_flow(uint16_t *flow);
/* 读取当前出水温度和流量 */
esp_err_t thermostat_read_water_temp_flow(uint16_t regs[3]);

#endif
