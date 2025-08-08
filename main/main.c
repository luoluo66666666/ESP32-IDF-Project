#include <stdio.h>
#include "wifi_module.h"
#include "Uart_module.h"
#include "app_msg.h"
#include "Task_manager.h"
#include "nimble/nimble_port_freertos.h"
#include "gap.h"
#include "ctrl_protocol.h"
#include "mode_ctrl.h"

static const char *TAG = "current MODE";

EventGroupHandle_t event_ctrl_protocol; // 事件组句柄，用于管理运行/故障/模式状态

void mode_control_task(void *pvParameters)
{
    EventBits_t bits;
    while (1)
    {
        bits = xEventGroupWaitBits(event_ctrl_protocol,
                                   Mode0_BIT | Mode1_BIT | Mode2_BIT | Mode3_BIT | Mode4_BIT | Mode5_UPPER_BIT | Mode5_LOWER_BIT,
                                   pdFALSE,        // 清除事件组标志位
                                   pdFALSE,        // 等待任意一个标志位
                                   portMAX_DELAY); // 无限等待
        switch (bits)
        {
        case Mode0_BIT:
            ESP_LOGI(TAG, "Mode 0 activated");
            mode0();                                              // 调用模式0的控制函数
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
            ESP_LOGI(TAG, "Mode 3 activated");
            mode4();                                              // 调用模式4的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode4_BIT); // 手动清除事件位
            break;
        case Mode5_UPPER_BIT:
            ESP_LOGI(TAG, "Mode 5 Upper activated");
            mode5_up(); // 调用模式5上半部分的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode5_UPPER_BIT); // 手动清除事件位
            break;
        case Mode5_LOWER_BIT:
            ESP_LOGI(TAG, "Mode 5 Lower activated");
            mode5_down();                                         // 调用模式5下半部分的控制函数
            xEventGroupClearBits(event_ctrl_protocol, Mode5_LOWER_BIT); // 手动清除事件位
            break;
        default:
            break;
        }
    }
}

void app_main(void)
{
    ctrl_protocol_init(); // Initialize the control protocol
    Wifi_task();          // 启动wifi模块

    ble_task(); // 启动BLE任务

    // 创建控制任务
    xTaskCreate(mode_control_task, "mode_ctrl", 4096, NULL, 10, NULL);
}
