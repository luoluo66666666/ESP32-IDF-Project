#include <stdio.h>
#include "wifi_module.h"
#include "Bluetooth_module.h"
#include "Uart_module.h"
#include "app_msg.h"
#include "Task_manager.h"
#include "nimble/nimble_port_freertos.h"


void app_main(void)
{
    // Wifi_start();  // 启动wifi模块

    ESP_LOGI(TAG, "App main start");
    
    int rc = nimble_port_init();
    ESP_LOGI(TAG, "nimble_port_init rc=%d", rc);
    if (rc != 0) return;

    rc = Bluetooth_startup_init();
    ESP_LOGI(TAG, "Bluetooth_startup_init rc=%d", rc);
    if (rc != 0) return;

    nimble_port_freertos_init(ble_host_task);


}
