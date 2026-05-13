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
#include "modbus.h"

static const char *TAG = "current MODE";

extern void Pole_motor_control_task(void *p);

EventGroupHandle_t event_ctrl_protocol; // 事件组句柄，用于管理运行、故障和模式状态


void mode_control_task(void *pvParameters)
{
    EventBits_t bits;
    while (1)
    {
        /* -----------检测 11 是否被关闭---------- */
        if (gpio_get_level(GPIO_NUM_45) == 0) // 若 TURN_OFF(11) 被调用
        {
            ESP_LOGW(TAG, "Warning: TURN 11 was OFF, restoring it.");
            TURN_ON(11);
        }
        bits = xEventGroupWaitBits(event_ctrl_protocol,
                                   Mode0_BIT | Mode1_BIT | Mode2_BIT | Mode3_BIT | Mode4_BIT | Mode5_UPPER_BIT | Mode5_LOWER_BIT,
                                   pdFALSE,        // 不清除事件组标志位
                                   pdFALSE,        // 等待任意一个标志位
                                   portMAX_DELAY); // 无限等待
        if (bits & Mode0_BIT)
        {
            ESP_LOGI(TAG, "Mode 0 activated");
            // start_mode0();
            // start_mode_test();
            mode4_zhongyao_v2();
            clear_mode_stop_request();
            xEventGroupClearBits(event_ctrl_protocol, Mode0_BIT);
        }
        else if (bits & Mode1_BIT)
        {
            ESP_LOGI(TAG, "Mode 1 activated");
            mode1();
            clear_mode_stop_request();
            xEventGroupClearBits(event_ctrl_protocol, Mode1_BIT);
        }
        else if (bits & Mode2_BIT)
        {
            ESP_LOGI(TAG, "Mode 2 activated");
            mode2();
            clear_mode_stop_request();
            xEventGroupClearBits(event_ctrl_protocol, Mode2_BIT);
        }
        else if (bits & Mode3_BIT)
        {
            ESP_LOGI(TAG, "Mode 3 activated");
            mode3();
            clear_mode_stop_request();
            xEventGroupClearBits(event_ctrl_protocol, Mode3_BIT);
        }
        else if (bits & Mode4_BIT)
        {
            ESP_LOGI(TAG, "Mode 4 activated");
            mode4();
            clear_mode_stop_request();
            xEventGroupClearBits(event_ctrl_protocol, Mode4_BIT);
        }
        else if (bits & Mode5_UPPER_BIT)
        {
            ESP_LOGI(TAG, "Mode 5 Upper activated");
            mode5_up();
            clear_mode_stop_request();
            xEventGroupClearBits(event_ctrl_protocol, Mode5_UPPER_BIT);
        }
        else if (bits & Mode5_LOWER_BIT)
        {
            ESP_LOGI(TAG, "Mode 5 Lower activated");
            mode5_down();
            clear_mode_stop_request();
            xEventGroupClearBits(event_ctrl_protocol, Mode5_LOWER_BIT);
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
    esp_err_t err;

    pin_init();
    ctrl_protocol_init(); // 初始化控制协议
    err = modbus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "modbus init failed: %s", esp_err_to_name(err));
    }

    err = temp_init_default_target();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "temp default init failed: %s", esp_err_to_name(err));
    }
    // Wifi_task();          // 启动 WiFi 模块
    // wifi_tcp_start();
    // wifi_ota_mode_start("http://192.168.1.125:8080/ESP-wash.bin");

    ble_task(); // 启动 BLE 任务
    // Temp_task();
    // Temp_task();
    // sensor_init();
    // // 创建控制任务
    // xTaskCreate(modbus_test_task, "modbus_test_task", 4096, NULL, 8, NULL);
    xTaskCreate(mode_control_task, "mode_ctrl", 4096, NULL, 10, NULL);
    xTaskCreate(Pole_motor_control_task, "Pole_motor_control", 4096, NULL, 10, NULL);
}
