#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "gatt_svc.h"
#include <driver/gpio.h>

#define TAG "RS485"
#define BUF_SIZE 127
#define ECHO_UART_PORT (CONFIG_ECHO_UART_PORT_NUM)
#define ECHO_TEST_TXD (CONFIG_ECHO_UART_TXD)
#define ECHO_TEST_RXD (CONFIG_ECHO_UART_RXD)
// #define ECHO_TEST_RTS (CONFIG_ECHO_UART_RTS)
// #define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)
#define UART_DE_GPIO   16   // DE 控制
#define UART_RE_GPIO   8   // RE 控制，低有效

extern QueueHandle_t ble_tx_queue;

static void echo_send(const int port, const uint8_t *data, size_t length);

// GPIO 初始化 DE/RE
void temp_RS485_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_ECHO_UART_RTS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // 默认接收模式
    gpio_set_level(CONFIG_ECHO_UART_RTS, 1);
}

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
    buf[6] = crc & 0xFF; // CRC 低字节
    buf[7] = crc >> 8;   // CRC 高字节

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

/**************************************************************************************/
static const unsigned char aucCRCHi[] = {
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40};
static const unsigned char aucCRCLo[] = {
    0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7,
    0x05, 0xC5, 0xC4, 0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E,
    0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09, 0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9,
    0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD, 0x1D, 0x1C, 0xDC,
    0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
    0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32,
    0x36, 0xF6, 0xF7, 0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D,
    0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A, 0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38,
    0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE, 0x2E, 0x2F, 0xEF,
    0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
    0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1,
    0x63, 0xA3, 0xA2, 0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4,
    0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F, 0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB,
    0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB, 0x7B, 0x7A, 0xBA,
    0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
    0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0,
    0x50, 0x90, 0x91, 0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97,
    0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C, 0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E,
    0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88, 0x48, 0x49, 0x89,
    0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
    0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83,
    0x41, 0x81, 0x80, 0x40};

unsigned short temp_MB_CRC16(unsigned char *pucFrame, unsigned short usLen) 
{ 
    unsigned char ucCRCHi = 0xFF; 
    unsigned char ucCRCLo = 0xFF; 
    unsigned short iIndex; 

    while(usLen--) 
    { 
        iIndex = ucCRCLo ^ *(pucFrame++); 
        ucCRCLo = (unsigned char)(ucCRCHi ^ aucCRCHi[iIndex]); 
        ucCRCHi = aucCRCLo[iIndex]; 
    } 

    // 返回 CRC16 高低字节组合
    return (unsigned short)((ucCRCHi << 8) | ucCRCLo); 
}

bool rs485_initialized = false;
//==================== 初始化 RS485 ====================//
void RS485_init(void)
{
    if (rs485_initialized)
    {
        ESP_LOGI(TAG, "RS485 already initialized");
        return;
    }

    uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT, ECHO_TEST_TXD, ECHO_TEST_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // 配置 DE/RE GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = ((1ULL << UART_DE_GPIO) | (1ULL << UART_RE_GPIO)),
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    gpio_config(&io_conf);

    // 初始为接收模式
    gpio_set_level(UART_DE_GPIO, 0);
    gpio_set_level(UART_RE_GPIO, 0);

    ESP_LOGI(TAG, "RS485 UART initialized with DE=%d RE=%d", UART_DE_GPIO, UART_RE_GPIO);

    rs485_initialized = true;

}

//==================== 发送报文 ====================//
static void temp_rs485_send(const uint8_t *data, size_t len)
{
    gpio_set_level(UART_DE_GPIO, 1);
    gpio_set_level(UART_RE_GPIO, 1); // 禁止接收器

    int ret = uart_write_bytes(ECHO_UART_PORT, (const char *)data, len);
    uart_wait_tx_done(ECHO_UART_PORT, pdMS_TO_TICKS(100));

    gpio_set_level(UART_DE_GPIO, 0);
    gpio_set_level(UART_RE_GPIO, 0); // 切回接收模式

    if (ret != len)
        ESP_LOGW(TAG, "Send incomplete (%d/%d)", ret, (int)len);

    ESP_LOGI(TAG, "TX -> (%d bytes)", (int)len);
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_INFO);
}

//==================== 接收报文 ====================//
static int temp_rs485_receive(uint8_t *rx_buf, int expected_len, int timeout_ms)
{
    int received = 0;
    TickType_t start = xTaskGetTickCount();

    while (received < expected_len)
    {
        int len = uart_read_bytes(ECHO_UART_PORT, rx_buf + received, expected_len - received, pdMS_TO_TICKS(50));
        if (len > 0)
            received += len;
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeout_ms))
            break;
    }

    if (received > 0)
    {
        ESP_LOGI(TAG, "RX <- (%d bytes)", received);
        ESP_LOG_BUFFER_HEXDUMP(TAG, rx_buf, received, ESP_LOG_INFO);
    }
    else
    {
        ESP_LOGW(TAG, "No response within %dms", timeout_ms);
    }

    return received;
}

//==================== 读寄存器 ====================//
void temp_rs485_read_register(uint8_t addr, uint16_t reg, uint16_t num_regs)
{
    uint8_t buf[8] = {addr, 0x03, reg >> 8, reg & 0xFF, num_regs >> 8, num_regs & 0xFF};
    uint16_t crc = temp_MB_CRC16(buf, 6);
    buf[6] = crc & 0xFF;
    buf[7] = crc >> 8;

    temp_rs485_send(buf, 8);

    uint8_t rx[64] = {0};
    int expected_len = 5 + num_regs * 2;
    int len = temp_rs485_receive(rx, expected_len, 500);

    if (len >= 5)
    {
        uint16_t recv_crc = rx[len - 2] | (rx[len - 1] << 8);
        if (temp_MB_CRC16(rx, len - 2) != recv_crc)
        {
            ESP_LOGW(TAG, "Invalid CRC");
            return;
        }
        int byte_count = rx[2];
        for (int i = 0; i < byte_count / 2; i++)
        {
            uint16_t val = (rx[3 + i * 2] << 8) | rx[4 + i * 2];
            ESP_LOGI(TAG, "Reg[%d] = %u (0x%04X)", i, val, val);
        }
    }
}

//==================== 写寄存器 ====================//
void temp_rs485_write_register(uint8_t addr, uint16_t reg, uint16_t value)
{
    uint8_t buf[8] = {addr, 0x06, reg >> 8, reg & 0xFF, value >> 8, value & 0xFF};
    uint16_t crc = temp_MB_CRC16(buf, 6);
    buf[6] = crc & 0xFF;
    buf[7] = crc >> 8;

    temp_rs485_send(buf, 8);

    uint8_t rx[8];
    int len = temp_rs485_receive(rx, 8, 200);
    if (len < 8)
    {
        ESP_LOGW(TAG, "Write response too short");
        return;
    }
}

//==================== 应用命令封装 ====================//
void temp_valve_power_on(uint8_t addr, uint8_t speed)
{
    uint16_t val = 0x0080 | (speed & 0x07);
    temp_rs485_write_register(addr, 0x0000, val);
    ESP_LOGI(TAG, "Power on (speed=%d)", speed);
}

void temp_valve_power_off(uint8_t addr)
{
    temp_rs485_write_register(addr, 0x0000, 0x0000);
    ESP_LOGI(TAG, "Power off");
}

void temp_valve_set_temperature(uint8_t addr, uint16_t temp)
{
    temp_rs485_write_register(addr, 0x0001, temp);
    ESP_LOGI(TAG, "Set temperature: %d°C", temp);
}

void temp_valve_read_water_temp(uint8_t addr)
{
    temp_rs485_read_register(addr, 0x0001, 1);
}

void temp_valve_read_flow(uint8_t addr)
{
    temp_rs485_read_register(addr, 0x0003, 1);
}



void temp_test_sequence(void)
{
    uint8_t addr = 1;

    // 1️⃣ 0x0000 寄存器写入 0x00C0 —— 开机 + 温度标志
    // temp_rs485_write_register(addr, 0x0000, 0x00C0);
    // vTaskDelay(pdMS_TO_TICKS(200));

    // // 2️⃣ 0x0001 寄存器写入 0x0023 —— 设置温度 35℃
    // temp_rs485_write_register(addr, 0x0001, 0x0025);
    // vTaskDelay(pdMS_TO_TICKS(200));

    // 3️⃣ 读取 0x0000 起始的 2 个寄存器（系统信息 + 当前温度）
    temp_rs485_read_register(addr, 0x0000, 2);
    vTaskDelay(pdMS_TO_TICKS(200));

    // 4️⃣ 读取单个寄存器 0x0002（当前设定温度）
    temp_rs485_read_register(addr, 0x0002, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    // 5️⃣ 读取 0x0001 起始的 4 个寄存器（温度/流量等连续数据）
    temp_rs485_read_register(addr, 0x0001, 4);
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Test sequence finished");
}



//==================== 周期任务 ====================//
void temp_valve_poll_task(void *arg)
{
    uint8_t addr = 1;

    // temp_valve_power_on(addr, 3);
    // temp_valve_set_temperature(addr, 40);

    while (1)
    {
        // ESP_LOGI(TAG, "------ Polling Valve ------");
        // temp_rs485_read_register(addr, 0x0001, 3);
        // // temp_rs485_read_register(addr, 0x0000, 1); // 状态寄存器
        // // vTaskDelay(pdMS_TO_TICKS(200));

        // // temp_valve_read_water_temp(addr);
        // vTaskDelay(pdMS_TO_TICKS(200));

        // temp_valve_read_flow(addr);
        temp_test_sequence();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}




void rs485_task(void)
{
    RS485_init();
    xTaskCreate(temp_valve_poll_task, "temp_valve_poll_task", 4096, NULL, 10, NULL);
}
