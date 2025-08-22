#ifndef __CTRL_PROTOCOL_H__
#define __CTRL_PROTOCOL_H__


#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"


#include <stdbool.h>


// 定义事件组标志位(最多32位)
#define Mode0_BIT BIT0
#define Mode1_BIT BIT1
#define Mode2_BIT BIT2
#define Mode3_BIT BIT3
#define Mode4_BIT BIT4
#define Mode5_UPPER_BIT BIT5
#define Mode5_LOWER_BIT BIT6
#define Motor_RUN_BIT BIT7
#define Motor_STOP_BIT BIT8
#define Motor_GET_BIT BIT9

#define RUN_BIT BIT10   // 运行状态标志位
#define FAULT_BIT BIT11 // 故障状态标志位


void ctrl_protocol_init(void);

bool get_fault_status(void);
bool get_run_status(void);
int get_mode_status(void);
int set_mode(int mode);
int check_status(void);
void ctrl_protocol(char *input,char *output,int maxlen );

int motor_run(void);
int motor_stop(void);



#endif // __CTRL_PROTOCOL_H__