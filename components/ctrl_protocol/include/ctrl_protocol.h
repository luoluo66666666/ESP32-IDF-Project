#ifndef __CTRL_PROTOCOL_H__
#define __CTRL_PROTOCOL_H__

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

/* 控制模式事件位 */
#define Mode0_BIT BIT0
#define Mode1_BIT BIT1
#define Mode2_BIT BIT2
#define Mode3_BIT BIT3
#define Mode4_BIT BIT4
#define Mode5_UPPER_BIT BIT5
#define Mode5_LOWER_BIT BIT6

/* 撑杆电机事件位 */
#define Motor_RUN_BIT BIT7
#define Motor_STOP_BIT BIT8
#define Motor_GET_BIT BIT9
#define RUN_BIT BIT10
#define FAULT_BIT BIT11
#define Motor_Finsh_BIT BIT12
#define MODE_STOP_BIT BIT13

void ctrl_protocol_init(void);
void ctrl_protocol(char *input, char *output, int maxlen);
void Pole_motor_control_task(void *p);

bool get_fault_status(void);
bool get_run_status(void);
int get_mode_status(void);
int set_mode(int mode);
int check_status(void);
bool mode_stop_requested(void);
void request_mode_stop(void);
void clear_mode_stop_request(void);

int motor_run(void);
int motor_stop(void);
int motor_finish(void);

void mode_light_init(void);

#endif
