#include <stdio.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbcontroller.h"

#include "modbus.h"

#define TAG "MODBUS"
#define MODBUS_UART_PORT (CONFIG_ECHO_UART_PORT_NUM)
#define MODBUS_UART_TXD (CONFIG_ECHO_UART_TXD)
#define MODBUS_UART_RXD (CONFIG_ECHO_UART_RXD)
#define MODBUS_UART_RTS (CONFIG_ECHO_UART_RTS)
#define MODBUS_UART_BAUD (CONFIG_ECHO_UART_BAUD_RATE)
#define MODBUS_RESPONSE_TIMEOUT_MS 1000
#define MASSAGE_CHAIR_SLAVE_ADDR 0x06
#define INVERTER_SLAVE_ADDR 0x03
#define THERMOSTAT_SLAVE_ADDR 0x01

#define MASSAGE_MODE_REG 0x0001
#define MASSAGE_STRENGTH_REG 0x0002

#define INVERTER_RUN_REG 0x2000
#define INVERTER_PRESSURE_SET_REG 0xF000
#define INVERTER_PRESSURE_READ_REG 0x1000

#define THERMOSTAT_TARGET_SET_REG 0x000C
#define THERMOSTAT_TARGET_READ_REG 0x0002
#define THERMOSTAT_WATER_TEMP_REG 0x0001
#define THERMOSTAT_WATER_FLOW_REG 0x0003

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
void modbus_init(void)
{
    esp_err_t err = modbus_lock(pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lock failed: %s", esp_err_to_name(err));
        return;
    }

    if (s_modbus_initialized) {
        modbus_unlock();
        return;
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
        ESP_LOGE(TAG, "mbc_master_create_serial failed: %s", esp_err_to_name(err));
        s_master_handle = NULL;
        modbus_unlock();
        return;
    }

    err = uart_set_pin(MODBUS_UART_PORT, MODBUS_UART_TXD, MODBUS_UART_RXD,
                       MODBUS_UART_RTS, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        mbc_master_delete(s_master_handle);
        s_master_handle = NULL;
        modbus_unlock();
        return;
    }

    err = uart_set_mode(MODBUS_UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_mode failed: %s", esp_err_to_name(err));
        mbc_master_delete(s_master_handle);
        s_master_handle = NULL;
        modbus_unlock();
        return;
    }

    err = mbc_master_start(s_master_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_master_start failed: %s", esp_err_to_name(err));
        mbc_master_delete(s_master_handle);
        s_master_handle = NULL;
        modbus_unlock();
        return;
    }

    s_modbus_initialized = true;
    ESP_LOGI(TAG, "master started on UART%d tx=%d rx=%d rts=%d baud=%d",
             MODBUS_UART_PORT, MODBUS_UART_TXD, MODBUS_UART_RXD, MODBUS_UART_RTS, MODBUS_UART_BAUD);
    modbus_unlock();
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

    modbus_init();
    if (!s_modbus_initialized) {
        return ESP_FAIL;
    }

    esp_err_t err = modbus_lock(pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS + 200));
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
    modbus_init();
    if (!s_modbus_initialized) {
        return ESP_FAIL;
    }

    esp_err_t err = modbus_lock(pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS + 200));
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

    modbus_init();
    if (!s_modbus_initialized) {
        return ESP_FAIL;
    }

    esp_err_t err = modbus_lock(pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS + 200));
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

    modbus_init();
    vTaskDelay(pdMS_TO_TICKS(1000));

    while (1) {
        esp_err_t err = modbus_read_holding_registers(0x01, 0x0000, 5, buffer);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Received data: %u %u %u %u %u",
                     buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
        } else {
            ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* 按摩椅停止 */
esp_err_t massage_chair_stop(void)
{
    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_MODE_REG, 0x0000);
}

/* 设置按摩椅模式 */
esp_err_t massage_chair_set_mode(massage_mode_t mode)
{
    if (mode > MASSAGE_MODE_3) {
        return ESP_ERR_INVALID_ARG;
    }

    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_MODE_REG, (uint16_t)mode);
}

/* 设置按摩椅低力度 */
esp_err_t massage_chair_set_strength_low(void)
{
    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_MODE_REG, 0x0002);
}

/* 设置按摩椅中力度 */
esp_err_t massage_chair_set_strength_medium(void)
{
    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_STRENGTH_REG, 0x0001);
}

/* 设置按摩椅高力度 */
esp_err_t massage_chair_set_strength_high(void)
{
    return modbus_write_single_register(MASSAGE_CHAIR_SLAVE_ADDR, MASSAGE_STRENGTH_REG, 0x0002);
}

/* 变频器启动 */
esp_err_t inverter_start(void)
{
    return modbus_write_single_register(INVERTER_SLAVE_ADDR, INVERTER_RUN_REG, 0x0001);
}

/* 变频器停止 */
esp_err_t inverter_stop(void)
{
    return modbus_write_single_register(INVERTER_SLAVE_ADDR, INVERTER_RUN_REG, 0x0005);
}

/* 设置变频器压力，单位 0.1 */
esp_err_t inverter_set_pressure_tenths(uint16_t pressure_x10)
{
    return modbus_write_single_register(INVERTER_SLAVE_ADDR, INVERTER_PRESSURE_SET_REG, pressure_x10);
}

/* 设置变频器压力 */
esp_err_t inverter_set_pressure(float pressure)
{
    if (pressure < 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    return inverter_set_pressure_tenths((uint16_t)(pressure * 10.0f + 0.5f));
}

/* 读取变频器原始压力值 */
esp_err_t inverter_read_pressure_raw(uint16_t *pressure_raw)
{
    return modbus_read_holding_registers(INVERTER_SLAVE_ADDR, INVERTER_PRESSURE_READ_REG, 1, pressure_raw);
}

/* 读取变频器压力 */
esp_err_t inverter_read_pressure(float *pressure)
{
    if (pressure == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw = 0;
    esp_err_t err = inverter_read_pressure_raw(&raw);
    if (err != ESP_OK) {
        return err;
    }

    *pressure = ((float)raw) / 10.0f;
    return ESP_OK;
}

/* 设置恒温室目标温度 */
esp_err_t thermostat_set_target_temperature(uint16_t temperature)
{
    return modbus_write_single_register(THERMOSTAT_SLAVE_ADDR, THERMOSTAT_TARGET_SET_REG, temperature);
}

/* 读取恒温室目标温度 */
esp_err_t thermostat_read_target_temperature(uint16_t *temperature)
{
    return modbus_read_holding_registers(THERMOSTAT_SLAVE_ADDR, THERMOSTAT_TARGET_READ_REG, 1, temperature);
}

/* 读取当前出水温度 */
esp_err_t thermostat_read_water_temperature(uint16_t *temperature)
{
    return modbus_read_holding_registers(THERMOSTAT_SLAVE_ADDR, THERMOSTAT_WATER_TEMP_REG, 1, temperature);
}

/* 读取当前出水流量 */
esp_err_t thermostat_read_water_flow(uint16_t *flow)
{
    return modbus_read_holding_registers(THERMOSTAT_SLAVE_ADDR, THERMOSTAT_WATER_FLOW_REG, 1, flow);
}

/* 读取当前出水温度和流量 */
esp_err_t thermostat_read_water_temp_flow(uint16_t regs[3])
{
    return modbus_read_holding_registers(THERMOSTAT_SLAVE_ADDR, THERMOSTAT_WATER_TEMP_REG, 3, regs);
}
