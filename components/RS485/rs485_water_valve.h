#ifndef __RS485_WATER_VALVE_H__
#define __RS485_WATER_VALVE_H__

void rs485_read_register(uint8_t addr, uint16_t reg, uint16_t num_regs);
void rs485_write_register(uint8_t addr, uint16_t reg, uint16_t value);
void temp_rs485_write_register(uint8_t addr, uint16_t reg, uint16_t value);
void temp_rs485_read_register(uint8_t addr, uint16_t reg, uint16_t num_regs);
void temp_test_sequence(void);
void RS485_init(void);
void temp_rs485_task(void);

bool temp_rs485_comm_test(uint8_t addr, uint16_t reg, uint16_t num_regs);
void temp_rs485_test_task(void *arg);

#endif