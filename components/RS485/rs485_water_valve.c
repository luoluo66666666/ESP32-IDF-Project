#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "gatt_svc.h"

#define TAG "RS485"
#define BUF_SIZE 127
#define ECHO_UART_PORT (CONFIG_ECHO_UART_PORT_NUM)
#define ECHO_TEST_TXD (CONFIG_ECHO_UART_TXD)
#define ECHO_TEST_RXD (CONFIG_ECHO_UART_RXD)
#define ECHO_TEST_RTS (CONFIG_ECHO_UART_RTS)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

extern QueueHandle_t ble_tx_queue;

static void echo_send(const int port, const uint8_t *data, size_t length);

static uint16_t modbus_crc16(const uint8_t *buf, int len)
{
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < len; pos++)
    {
        crc ^= buf[pos];
        for (int i = 0; i < 8; i++)
        {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}

static void echo_send(const int port, const uint8_t *data, size_t length)
{
    int ret = uart_write_bytes(port, (const char *)data, length);
    if (ret != length)
    {
        ESP_LOGE(TAG, "RS485 send failed (%d/%d)", ret, (int)length);
    }
}

void rs485_write_register(uint8_t addr, uint16_t reg, uint16_t value)
{
    const int uart_num = ECHO_UART_PORT;
    uint8_t buf[8] = {addr, 0x06, reg >> 8, reg, value >> 8, value};
    uint16_t crc = modbus_crc16(buf, 6);
    buf[6] = crc;
    buf[7] = crc >> 8;
    echo_send(uart_num, buf, 8);
}

void rs485_read_register(uint8_t addr, uint16_t reg, uint16_t num_regs)
{
    const int uart_num = ECHO_UART_PORT;
    uint8_t buf[8] = {addr, 0x03, reg >> 8, reg, num_regs >> 8, num_regs};
    uint16_t crc = modbus_crc16(buf, 6);
    buf[6] = crc & 0xFF;      // CRC 低字节
    buf[7] = crc >> 8;        // CRC 高字节

    // 发送请求帧
    echo_send(uart_num, buf, 8);
    ESP_LOGI(TAG, "TX -> Modbus Request (addr=0x%02X, reg=0x%04X, num=%d)", addr, reg, num_regs);
    ESP_LOG_BUFFER_HEXDUMP("TX", buf, 8, ESP_LOG_INFO);

    // 计算预期接收长度
    int expected_len = 5 + num_regs * 2; // addr + func + byte_count + 数据 + CRC

    // 循环读取完整帧
    uint8_t data[BUF_SIZE];
    int received = 0;
    TickType_t start_tick = xTaskGetTickCount();
    while (received < expected_len)
    {
        int len = uart_read_bytes(uart_num, data + received, expected_len - received, pdMS_TO_TICKS(50));
        if (len > 0)
        {
            received += len;
        }
        // 超时 500ms
        if ((xTaskGetTickCount() - start_tick) > pdMS_TO_TICKS(500))
        {
            break;
        }
    }

    ESP_LOGI(TAG, "Read len = %d", received);
    if (received >= 5)
    {
        ESP_LOG_BUFFER_HEXDUMP("RX", data, received, ESP_LOG_INFO);

        // CRC 校验
        uint16_t recv_crc = data[received - 2] | (data[received - 1] << 8);
        if (modbus_crc16(data, received - 2) != recv_crc)
        {
            ESP_LOGW(TAG, "Invalid CRC");
            return; // CRC 错误直接返回
        }

        // 检查功能码
        if (data[1] == 0x03)
        {
            int byte_count = data[2];
            ESP_LOGI(TAG, "Valid response, byte_count = %d", byte_count);

            for (int i = 0; i < byte_count / 2; i++)
            {
                uint16_t val = (data[3 + i * 2] << 8) | data[4 + i * 2];
                ESP_LOGI(TAG, "Reg[%d] = %u (0x%04X)", i, val, val);
            }

            // BLE发送逻辑保持不变
            ble_data_t tx_data = {0};
            int pos = 0;
            for (int i = 0; i < byte_count / 2; i++)
            {
                uint16_t val = (data[3 + i * 2] << 8) | data[4 + i * 2];
                int n = snprintf((char *)tx_data.buf + pos, sizeof(tx_data.buf) - pos,
                                 "Reg[%d]=%u ", i, val);
                if (n < 0 || pos + n >= sizeof(tx_data.buf) - 1)
                    break;
                pos += n;
            }
            tx_data.len = pos;

            if (ble_tx_queue != NULL)
            {
                if (xQueueSend(ble_tx_queue, &tx_data, 10 / portTICK_PERIOD_MS) != pdPASS)
                {
                    ESP_LOGW(TAG, "BLE queue full, data dropped");
                }
                else
                {
                    ESP_LOGI(TAG, "BLE queued: %.*s", tx_data.len, tx_data.buf);
                }
            }
        }
        else
        {
            ESP_LOGW(TAG, "Invalid Modbus response (function=0x%02X)", data[1]);
        }
    }
    else
    {
        ESP_LOGW(TAG, "No response or too short from slave");
    }
}



void RS485_init(void)
{
    const uart_port_t uart_num = ECHO_UART_PORT;

    // 检查 UART 驱动是否已经安装
    if (uart_is_driver_installed(uart_num)) {
        ESP_LOGW(TAG, "RS485 UART already initialized, skip reinstall");
        return; // 已安装就直接返回
    }

    uart_config_t cfg = {
        .baud_rate = CONFIG_ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    // 安装 UART 驱动
    esp_err_t err = uart_driver_install(uart_num, BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return;
    }

    //  配置 UART 参数
    uart_param_config(uart_num, &cfg);

    // 设置引脚
    uart_set_pin(uart_num, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);

    // 设置为半双工 RS485 模式
    uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX);

    ESP_LOGI(TAG, "RS485 UART initialized successfully");
}


// void app_main(void)
// {
//     RS485_init();
//     while (1) {
//         rs485_write_register(1, 0x0005, 50); // 设置50%开度
//         vTaskDelay(pdMS_TO_TICKS(2000));
//         rs485_read_register(1, 0x0000, 5);
//         vTaskDelay(pdMS_TO_TICKS(5000));
//     }
// }
