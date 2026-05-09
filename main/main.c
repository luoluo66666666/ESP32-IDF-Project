#include <stdio.h>
#include "wifi_module.h"
#include "Uart_module.h"
#include "app_msg.h"
#include "Task_manager.h"
#include "nimble/nimble_port_freertos.h"
#include "gap.h"
#include "ctrl_protocol.h"
#include "mode_ctrl.h"
#include <esp_log.h>
#include "rs485_water_valve.h"

static const char *TAG = "current MODE";

extern void Pole_motor_control_task(void *p);

EventGroupHandle_t event_ctrl_protocol; // 事件组句柄，用于管理运行/故障/模式状态

void mode_control_task(void *pvParameters)
{
    EventBits_t bits;
    while (1)
    {
        /* -----------检测 11 是否被关闭----------- */
        if (gpio_get_level(GPIO_NUM_45) == 0) // 若 TURN_OFF(11) 被调用
        {
            ESP_LOGW(TAG, "Warning: TURN 11 was OFF, restoring it.");
            TURN_ON(11);
        }
        bits = xEventGroupWaitBits(event_ctrl_protocol,
                                   Mode0_BIT | Mode1_BIT | Mode2_BIT | Mode3_BIT | Mode4_BIT | Mode5_UPPER_BIT | Mode5_LOWER_BIT,
                                   pdFALSE,        // 清除事件组标志位
                                   pdFALSE,        // 等待任意一个标志位
                                   portMAX_DELAY); // 无限等待
        switch (bits)
        {
        case Mode0_BIT:
            ESP_LOGI(TAG, "Mode 0 activated");
            // start_mode0();
            start_mode_test();                                    // 调用模式0的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode0_BIT); // 手动清除事件位
            break;
        case Mode1_BIT:
            ESP_LOGI(TAG, "Mode 1 activated");
            mode1();                                              // 调用模式1的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode1_BIT); // 手动清除事件位
            break;
        case Mode2_BIT:
            ESP_LOGI(TAG, "Mode 2 activated");
            mode2();                                              // 调用模式2的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode2_BIT); // 手动清除事件位
            break;
        case Mode3_BIT:
            ESP_LOGI(TAG, "Mode 3 activated");
            mode3();                                              // 调用模式3的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode3_BIT); // 手动清除事件位
            break;
        case Mode4_BIT:
            ESP_LOGI(TAG, "Mode 4 activated");
            mode4();                                              // 调用模式4的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode4_BIT); // 手动清除事件位
            break;
        case Mode5_UPPER_BIT:
            ESP_LOGI(TAG, "Mode 5 Upper activated");
            mode5_up();                                                 // 调用模式5上半部分的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode5_UPPER_BIT); // 手动清除事件位
            break;
        case Mode5_LOWER_BIT:
            ESP_LOGI(TAG, "Mode 5 Lower activated");
            mode5_down();                                               // 调用模式5下半部分的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode5_LOWER_BIT); // 手动清除事件位
            break;
        default:
            break;
        }
    }
}
/*******************************************************************************
****@brief: 
****@author: LuoLuo
****@date: 2026-01-27 10:30:16
********************************************************************************/
void app_main(void)
{
    // pin_init();
    ctrl_protocol_init(); // Initialize the control protocol
    // Wifi_task();          // 启动wifi模块
    // wifi_tcp_start();
    // wifi_ota_mode_start("http://192.168.1.125:8080/ESP-wash.bin");

    ble_task(); // 启动BLE任务
    // Temp_task();
    // Temp_task();
    // sensor_init();
    temp_rs485_task();   //恒温宝心跳包
    // xTaskCreate(temp_rs485_test_task, "rs485_test", 4096, NULL, 8, NULL);
    // // 创建控制任务
    xTaskCreate(mode_control_task, "mode_ctrl", 4096, NULL, 10, NULL);
    xTaskCreate(Pole_motor_control_task, "Pole_motor_control", 4096, NULL, 10, NULL);
}
