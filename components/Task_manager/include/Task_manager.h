#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"

#define MAX_QUEUE_MSG_SIZE 256

extern QueueHandle_t uart_rx_queue;
extern QueueHandle_t uart_tx_queue;
extern QueueHandle_t bt_rx_queue;
extern QueueHandle_t bt_tx_queue;

void task_manager_init(void);

