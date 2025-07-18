#ifndef __MODE_CTRL_H__
#define __MODE_CTRL_H__

#include "driver/gpio.h"

esp_err_t pin_init(void);
int set_do_pin(int index, int level);
int get_do_pin(int index);
int get_di_pin(int index);

int mode0(void);
int mode1(void);
int mode2(void);
int mode3(void);
int mode4(void);
int mode5_up(void);
int mode5_down(void);

#endif // __MODE_CTRL_H__