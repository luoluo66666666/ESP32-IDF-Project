#include "Task_manager.h"

QueueHandle_t uart_rx_queue = NULL;
QueueHandle_t uart_tx_queue = NULL;
QueueHandle_t bt_rx_queue = NULL;
QueueHandle_t bt_tx_queue = NULL;

void task_manager_init(void)
{
    uart_rx_queue = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
    uart_tx_queue = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
    bt_rx_queue   = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
    bt_tx_queue   = xQueueCreate(10, MAX_QUEUE_MSG_SIZE);
}

