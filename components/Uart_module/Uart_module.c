#include "driver/uart.h"
#include "Task_manager.h"

#define UART_NUM UART_NUM_1
#define BUF_SIZE 256

static void uart_task(void *arg)
{
    uint8_t data[BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(UART_NUM, data, BUF_SIZE, 100);
        if (len > 0) {
            data[len] = '\0';
            xQueueSend(uart_rx_queue, data, 0);
        }

        // 发送队列检查
        if (uxQueueMessagesWaiting(uart_tx_queue)) {
            char send_data[BUF_SIZE];
            if (xQueueReceive(uart_tx_queue, send_data, 0)) {
                uart_write_bytes(UART_NUM, send_data, strlen(send_data));
            }
        }
    }
}

void uart_module_start(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE); // 修改为实际引脚

    xTaskCreate(uart_task, "uart_task", 4096, NULL, 10, NULL);
}
