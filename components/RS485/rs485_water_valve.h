#ifndef __RS485_WATER_VALVE_H__
#define __RS485_WATER_VALVE_H__

void rs485_read_register(uint8_t addr, uint16_t reg, uint16_t num_regs);
void rs485_write_register(uint8_t addr, uint16_t reg, uint16_t value);
void RS485_init(void);
void rs485_task(void);

void temp_valve_read_status(uint8_t addr);

#endif