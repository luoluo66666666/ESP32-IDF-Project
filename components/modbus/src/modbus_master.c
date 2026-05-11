#include <stdio.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbcontroller.h"

#include "modbus_master.h"

#define TAG "MODBUS"
#define MODBUS_UART_PORT (CONFIG_ECHO_UART_PORT_NUM)
#define MODBUS_UART_TXD (CONFIG_ECHO_UART_TXD)
#define MODBUS_UART_RXD (CONFIG_ECHO_UART_RXD)
#define MODBUS_UART_RTS (CONFIG_ECHO_UART_RTS)
#define MODBUS_UART_BAUD (CONFIG_ECHO_UART_BAUD_RATE)
#define MODBUS_RESPONSE_TIMEOUT_MS 1000

static void *s_master_handle = NULL;
static SemaphoreHandle_t s_modbus_mutex = NULL;
static bool s_modbus_initialized = false;

/* 获取 Modbus 锁 */
static esp_err_t modbus_lock(TickType_t timeout_ticks)
{
    if (s_modbus_mutex == NULL) {
        s_modbus_mutex = xSemaphoreCreateMutex();
        if (s_modbus_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_modbus_mutex, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

/* 释放 Modbus 锁 */
static void modbus_unlock(void)
{
    if (s_modbus_mutex != NULL) {
        xSemaphoreGive(s_modbus_mutex);
    }
}

/* 初始化 Modbus 主站 */
esp_err_t modbus_init(void)
{
    esp_err_t err = modbus_lock(pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lock failed: %s", esp_err_to_name(err));
        return err;
    }

    if (s_modbus_initialized) {
        modbus_unlock();
        return ESP_OK;
    }

    mb_communication_info_t comm = {
        .ser_opts.port = MODBUS_UART_PORT,
        .ser_opts.mode = MB_RTU,
        .ser_opts.baudrate = MODBUS_UART_BAUD,
        .ser_opts.parity = UART_PARITY_DISABLE,
        .ser_opts.uid = 0,
        .ser_opts.response_tout_ms = MODBUS_RESPONSE_TIMEOUT_MS,
        .ser_opts.data_bits = UART_DATA_8_BITS,
        .ser_opts.stop_bits = UART_STOP_BITS_1
    };

    err = mbc_master_create_serial(&comm, &s_master_handle);
    if (err != ESP_OK || s_master_handle == NULL) {
        if (err == ESP_OK) {
            err = ESP_FAIL;
        }
        ESP_LOGE(TAG, "mbc_master_create_serial failed: %s", esp_err_to_name(err));
        s_master_handle = NULL;
        modbus_unlock();
        return err;
    }

    err = uart_set_pin(MODBUS_UART_PORT, MODBUS_UART_TXD, MODBUS_UART_RXD,
                       MODBUS_UART_RTS, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        mbc_master_delete(s_master_handle);
        s_master_handle = NULL;
        modbus_unlock();
        return err;
    }

    err = uart_set_mode(MODBUS_UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_mode failed: %s", esp_err_to_name(err));
        mbc_master_delete(s_master_handle);
        s_master_handle = NULL;
        modbus_unlock();
        return err;
    }

    err = mbc_master_start(s_master_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_master_start failed: %s", esp_err_to_name(err));
        mbc_master_delete(s_master_handle);
        s_master_handle = NULL;
        modbus_unlock();
        return err;
    }

    s_modbus_initialized = true;
    ESP_LOGI(TAG, "master started on UART%d tx=%d rx=%d rts=%d baud=%d",
             MODBUS_UART_PORT, MODBUS_UART_TXD, MODBUS_UART_RXD, MODBUS_UART_RTS, MODBUS_UART_BAUD);
    modbus_unlock();
    return ESP_OK;
}

/* 查询 Modbus 是否已初始化 */
bool modbus_is_initialized(void)
{
    return s_modbus_initialized;
}

/* 读取保持寄存器 */
esp_err_t modbus_read_holding_registers(uint8_t slave_addr, uint16_t reg_start,
                                        uint16_t reg_count, uint16_t *buffer)
{
    if (buffer == NULL || reg_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = modbus_init();
    if (err != ESP_OK) {
        return err;
    }

    err = modbus_lock(pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS + 200));
    if (err != ESP_OK) {
        return err;
    }

    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command = MODBUS_FC_READ_HOLDING_REGISTERS,
        .reg_start = reg_start,
        .reg_size = reg_count
    };

    err = mbc_master_send_request(s_master_handle, &req, buffer);
    modbus_unlock();
    return err;
}

/* 写单个寄存器 */
esp_err_t modbus_write_single_register(uint8_t slave_addr, uint16_t reg_addr, uint16_t value)
{
    esp_err_t err = modbus_init();
    if (err != ESP_OK) {
        return err;
    }

    err = modbus_lock(pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS + 200));
    if (err != ESP_OK) {
        return err;
    }

    uint16_t write_value = value;
    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command = MODBUS_FC_WRITE_SINGLE_REGISTER,
        .reg_start = reg_addr,
        .reg_size = 1
    };

    err = mbc_master_send_request(s_master_handle, &req, &write_value);
    modbus_unlock();
    return err;
}

/* 写多个寄存器 */
esp_err_t modbus_write_multiple_registers(uint8_t slave_addr, uint16_t reg_start,
                                          const uint16_t *buffer, uint16_t reg_count)
{
    if (buffer == NULL || reg_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = modbus_init();
    if (err != ESP_OK) {
        return err;
    }

    err = modbus_lock(pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS + 200));
    if (err != ESP_OK) {
        return err;
    }

    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command = MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
        .reg_start = reg_start,
        .reg_size = reg_count
    };

    err = mbc_master_send_request(s_master_handle, &req, (void *)buffer);
    modbus_unlock();
    return err;
}

/* Modbus 通讯测试 */
bool modbus_comm_test(uint8_t slave_addr, uint16_t reg_start, uint16_t reg_count)
{
    if (reg_count == 0 || reg_count > 32) {
        return false;
    }

    uint16_t buffer[32] = {0};
    esp_err_t err = modbus_read_holding_registers(slave_addr, reg_start, reg_count, buffer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "comm test failed: %s", esp_err_to_name(err));
        return false;
    }

    for (uint16_t i = 0; i < reg_count; i++) {
        ESP_LOGI(TAG, "Reg[%u] = %u (0x%04X)", i, buffer[i], buffer[i]);
    }

    return true;
}

/* Modbus 测试任务 */
void modbus_test_task(void *arg)
{
    uint16_t buffer[5] = {0};

    esp_err_t err = modbus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        err = modbus_read_holding_registers(0x01, 0x0000, 5, buffer);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Received data: %u %u %u %u %u",
                     buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
        } else {
            ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
