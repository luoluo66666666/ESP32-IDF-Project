// cmd_handler.c
#include "app_msg.h"
#include "Task_manager.h"
#include <string.h>
#include <stdio.h>

// QueueHandle_t rx_queue;
// QueueHandle_t tx_queue;

// void app_msg_init(void)
// {
//     rx_queue = xQueueCreate(10, sizeof(app_msg_t));
//     tx_queue = xQueueCreate(10, sizeof(app_msg_t));
// }


// static void cmd_task(void *arg)
// {
//     app_msg_t msg;
//     while (1) {
//         if (xQueueReceive(rx_queue, &msg, portMAX_DELAY)) {
//             if (msg.src == MSG_SRC_UART) {
//                 if (strncmp(msg.data, "SET_BT_NAME:", 12) == 0) {
//                     char *name = msg.data + 12;
//                     esp_bt_dev_set_device_name(name);
//                     printf("BT name changed to: %s\n", name);
//                 }
//                 // TODO: 支持更多指令如开关蓝牙、改模式等
//             }

//             // 其他命令也可处理后转发
//         }
//     }
// }

// void start_cmd_handler(void)
// {
//     xTaskCreate(cmd_task, "cmd_handler", 4096, NULL, 10, NULL);
// }
