// // app_msg.h
// #pragma once
// #include "freertos/queue.h"

// typedef enum {
//     MSG_SRC_UART,
//     MSG_SRC_WIFI,
//     MSG_SRC_BT
// } msg_source_t;

// typedef struct {
//     msg_source_t src;
//     char data[256];
// } app_msg_t;

// extern QueueHandle_t rx_queue;
// extern QueueHandle_t tx_queue;

// void app_msg_init(void);

// void start_cmd_handler(void);

