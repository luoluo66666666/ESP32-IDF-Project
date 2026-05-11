#ifndef __MODBUS_MASTER_H__
#define __MODBUS_MASTER_H__

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

/* 初始化 Modbus 主站 */
esp_err_t modbus_init(void);

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

#endif
