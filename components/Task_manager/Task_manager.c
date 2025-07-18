#include "Task_manager.h"

// module事件组定义
QueueHandle_t uart_rx_queue = NULL; // uart接收队列
QueueHandle_t uart_tx_queue = NULL; // uart发送队列
QueueHandle_t ble_rx_queue = NULL;  // bt接收队列
QueueHandle_t ble_tx_queue = NULL;  // bt接收队列
QueueHandle_t wifi_rx_queue = NULL; // wifi接收队列
QueueHandle_t wifi_tx_queue = NULL; // wifi发送队列

void task_manager_init(void)
{
    uart_rx_queue = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
    uart_tx_queue = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
    ble_rx_queue = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
    ble_tx_queue = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
    wifi_rx_queue = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
    wifi_tx_queue = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
}
