#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "device_reg.h"

#include "string.h"

static const char *TAG_MASTER = "I2C_MASTER";
static const char *TAG_SLAVE = "I2C_SLAVE";

thermostat_t thermo; // 定义
mlb_device_t dev;    // 定义
// 主机 I2C
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SCL 17
#define I2C_MASTER_SDA 18

// 从机 I2C
#define I2C_SLAVE_NUM I2C_NUM_1
#define I2C_SLAVE_SCL 13
#define I2C_SLAVE_SDA 14
#define I2C_SLAVE_ADDR 0x50

#define I2C_FREQ_HZ 100000

/*******************************************************************************
****@brief: i2c主机初始化
****@author: Luo
****@date: 2025-10-13 08:40:34
********************************************************************************/
// ------------------ 旧 driver 初始化 ------------------
esp_err_t my_i2c_master_init(my_i2c_device_handle_t *out_dev)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA,
        .scl_io_num = I2C_MASTER_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ};
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, 0, 0, 0));

    my_i2c_device_handle_t dev = calloc(1, sizeof(my_i2c_device_t));
    dev->i2c_num = I2C_MASTER_NUM; // 保存端口号
    dev->addr_bits = 3;
    dev->data_bits = 8;
    dev->write_delay_ms = 5;
    memset(dev->buf, 0, sizeof(dev->buf));

    *out_dev = dev;
    ESP_LOGI(TAG_MASTER, "I2C master initialized SDA=%d, SCL=%d", I2C_MASTER_SDA, I2C_MASTER_SCL);
    return ESP_OK;
}

esp_err_t i2c_slave_init(void)
{
    // 删除可能残留的驱动
    i2c_driver_delete(I2C_SLAVE_NUM);

    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SLAVE_SDA,
        .scl_io_num = I2C_SLAVE_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = I2C_SLAVE_ADDR};

    esp_err_t err = i2c_param_config(I2C_SLAVE_NUM, &conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_SLAVE, "i2c_param_config failed: %d", err);
        return err;
    }

    err = i2c_driver_install(I2C_SLAVE_NUM, I2C_MODE_SLAVE, 256, 256, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_SLAVE, "i2c_driver_install failed: %d", err);
        return err;
    }

    ESP_LOGI(TAG_SLAVE, "I2C slave initialized SDA=%d SCL=%d addr=0x%02X",
             I2C_SLAVE_SDA, I2C_SLAVE_SCL, I2C_SLAVE_ADDR);
    return ESP_OK;
}

// ------------------ I2C 协议栈 ------------------
void i2c_eeprom_wait_idle(my_i2c_device_handle_t dev)
{
    vTaskDelay(pdMS_TO_TICKS(dev->write_delay_ms));
}

esp_err_t i2c_write_reg(my_i2c_device_handle_t dev, uint8_t addr, uint8_t data)
{
    if (!dev)
        return ESP_ERR_INVALID_ARG;

    uint32_t value = 0;
    value |= (0 << 19);
    value |= ((addr & 0x7) << 16);
    value |= ((data & 0xFF) << 8);
    value |= ((data ^ 0xFF) & 0xFF);

    for (int i = 0; i < 3; i++)
        dev->buf[i] = (value >> (8 * (2 - i))) & 0xFF;

    return i2c_master_write_to_device(dev->i2c_num, I2C_SLAVE_ADDR,
                                      dev->buf, sizeof(dev->buf),
                                      pdMS_TO_TICKS(1000));
}

esp_err_t i2c_read_reg(my_i2c_device_handle_t dev, uint8_t addr, uint8_t *out)
{
    if (!dev || !out)
        return ESP_ERR_INVALID_ARG;

    uint8_t write_buf[3] = {1 << 2, 0, 0xFF};
    uint8_t read_buf[3];

    esp_err_t ret = i2c_master_write_to_device(dev->i2c_num, I2C_SLAVE_ADDR,
                                               write_buf, sizeof(write_buf),
                                               pdMS_TO_TICKS(1000));
    if (ret != ESP_OK)
        return ret;

    ret = i2c_master_read_from_device(dev->i2c_num, I2C_SLAVE_ADDR,
                                      read_buf, sizeof(read_buf),
                                      pdMS_TO_TICKS(1000));
    if (ret != ESP_OK)
        return ret;

    if (read_buf[2] != (uint8_t)(~read_buf[1]))
        return ESP_ERR_INVALID_RESPONSE;
    *out = read_buf[1];
    return ESP_OK;
}

// ------------------ 从机任务 ------------------
void i2c_slave_task(void *arg)
{
    uint8_t data[3];
    while (1)
    {
        int len = i2c_slave_read_buffer(I2C_SLAVE_NUM, data, sizeof(data),
                                        pdMS_TO_TICKS(500));
        if (len > 0)
        {
            ESP_LOGI(TAG_SLAVE, "Recv %d bytes: %02X %02X %02X", len, data[0], data[1], data[2]);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// void i2c_eeprom_wait_idle(my_i2c_device_handle_t dev)
// {
//     // This is time for EEPROM Self-Timed Write Cycle
//     vTaskDelay(pdMS_TO_TICKS(dev->write_delay_ms));
// }
// /*******************************************************************************
// ****@brief:
// ****@author: Luo
// ****@date: 2025-10-13 08:41:23
// ********************************************************************************/
// esp_err_t i2c_write_reg(my_i2c_device_handle_t dev, uint8_t addr, uint8_t data)
// {
//     if (!dev) return ESP_ERR_INVALID_ARG;

//     uint32_t value = 0;
//     value |= (0 << 19);
//     value |= ((addr & 0x7) << 16);
//     value |= ((data & 0xFF) << 8);
//     value |= ((data ^ 0xFF) & 0xFF);

//     for (int i = 0; i < 3; i++)
//         dev->buf[i] = (value >> (8 * (2 - i))) & 0xFF;

//     return i2c_master_transmit(dev->dev, dev->buf, sizeof(dev->buf), pdMS_TO_TICKS(1000));
// }

// esp_err_t i2c_read_reg(my_i2c_device_handle_t dev, uint8_t addr, uint8_t *out)
// {
//     if (!dev || !out) return ESP_ERR_INVALID_ARG;

//     uint8_t write_buf[3];
//     uint8_t read_buf[3];
//     write_buf[0] = (1 << 2);
//     write_buf[1] = 0;
//     write_buf[2] = 0xFF;

//     // 写请求
//     esp_err_t ret = i2c_master_transmit(dev->dev, write_buf, sizeof(write_buf), pdMS_TO_TICKS(1000));
//     if (ret != ESP_OK) return ret;

//     // 读响应
//     ret = i2c_master_receive(dev->dev, read_buf, sizeof(read_buf), pdMS_TO_TICKS(1000));
//     if (ret != ESP_OK) return ret;

//     if (read_buf[2] != (uint8_t)(~read_buf[1])) return ESP_ERR_INVALID_RESPONSE;
//     *out = read_buf[1];
//     return ESP_OK;
// }

// 设置温度/模式
esp_err_t mlb_set_temperature(mlb_device_t *mlb, uint8_t temp)
{
    if (!mlb)
        return ESP_ERR_INVALID_ARG;
    mlb->wdata.temperature.temp_set = temp;
    return i2c_write_reg(mlb->dev, MLB_ADDR_TEMP_MODE, temp);
}

// 读取温度
esp_err_t mlb_get_temperature(mlb_device_t *mlb, uint8_t *temp)
{
    if (!mlb || !temp)
        return ESP_ERR_INVALID_ARG;
    uint8_t val;
    esp_err_t ret = i2c_read_reg(mlb->dev, MLB_ADDR_TEMP_MODE, &val);
    if (ret == ESP_OK)
        *temp = val & 0x7F;
    return ret;
}

// 设置系统信息（速度/开关机）
esp_err_t mlb_set_system_info(mlb_device_t *mlb, uint8_t data)
{
    if (!mlb)
        return ESP_ERR_INVALID_ARG;
    mlb->wdata.system_info.power = (data >> 7) & 0x01;
    mlb->wdata.system_info.temp_flag = (data >> 6) & 0x01;
    mlb->wdata.system_info.speed_flag = (data >> 5) & 0x01;
    mlb->wdata.system_info.reserved2 = (data >> 4) & 0x01;
    mlb->wdata.system_info.reserved1 = (data >> 3) & 0x01;
    mlb->wdata.system_info.speed_mode = data & 0x07;
    return i2c_write_reg(mlb->dev, MLB_ADDR_SYS, data);
}

// 读取系统状态
esp_err_t mlb_get_system_status(mlb_device_t *mlb, uint8_t *status)
{
    if (!mlb || !status)
        return ESP_ERR_INVALID_ARG;
    return i2c_read_reg(mlb->dev, MLB_ADDR_SYS, status);
}

// 设置流量校准
esp_err_t mlb_set_flow_calib(mlb_device_t *mlb, uint8_t flow)
{
    if (!mlb)
        return ESP_ERR_INVALID_ARG;
    mlb->wdata.flow.flow_calib = flow;
    return i2c_write_reg(mlb->dev, MLB_ADDR_CALIB_FLOW, flow);
}

// 读取流量
esp_err_t mlb_get_flow(mlb_device_t *mlb, uint8_t *flow)
{
    if (!mlb || !flow)
        return ESP_ERR_INVALID_ARG;
    return i2c_read_reg(mlb->dev, MLB_ADDR_FLOW, flow);
}

void thermostat_init(thermostat_t *thermo, mlb_device_t *dev, int target_temp, int temp_tolerance, int adjust_speed, int maintain_speed)
{
    if (!thermo)
        return;
    thermo->dev = dev;
    thermo->target_temp = target_temp;
    thermo->temp_tolerance = temp_tolerance;
    thermo->adjust_speed = adjust_speed;
    thermo->maintain_speed = maintain_speed;
    thermo->current_temp = 0;
    thermo->current_speed = 0;
    thermo->heating = false;
}

int thermostat_get_temperature(thermostat_t *thermo)
{
    if (!thermo || !thermo->dev)
        return 0;
    int temp = 0;
    mlb_get_temperature(thermo->dev, (uint8_t *)&temp);
    thermo->current_temp = temp;
    return temp;
}

void thermostat_update(thermostat_t *thermo)
{
    int cur_temp = thermostat_get_temperature(thermo);
    bool sensor_fault = (thermo->dev->rdata.host_status2.temp_sensor_fault != 0);

    if (sensor_fault)
    {
        mlb_set_temperature(thermo->dev, 0); // 停止加热
        thermo->heating = false;
        return;
    }

    int diff = thermo->target_temp - cur_temp;
    int speed = thermo->maintain_speed;

    if (diff > thermo->temp_tolerance)
    {
        mlb_set_temperature(thermo->dev, 49); // 热水模式
        speed = thermo->adjust_speed;
        thermo->heating = true;
    }
    else if (diff < -thermo->temp_tolerance)
    {
        mlb_set_temperature(thermo->dev, 24); // 冷水模式
        speed = thermo->adjust_speed;
        thermo->heating = false;
    }
    else
    {
        mlb_set_temperature(thermo->dev, thermo->target_temp);
        speed = thermo->maintain_speed;
        thermo->heating = diff > 0; // 微调加热状态
    }

    mlb_set_system_info(thermo->dev, speed & 0x07);
    thermo->current_speed = speed;
    thermo->current_temp = cur_temp;
}

void thermostat_handle_cmd(thermostat_t *thermo, const char *cmd, char *response, size_t resp_len)
{
    if (!thermo || !cmd || !response)
        return;

    if (strncmp(cmd, "SET TEMP ", 9) == 0)
    {
        int temp = atoi(cmd + 9);
        thermo->target_temp = temp;
        snprintf(response, resp_len, "Target temp set: %d", temp);
    }
    else if (strncmp(cmd, "SET ADJ ", 8) == 0)
    {
        int adj = atoi(cmd + 8);
        thermo->adjust_speed = adj;
        snprintf(response, resp_len, "Adjust speed set: %d", adj);
    }
    else if (strncmp(cmd, "SET MAINT ", 10) == 0)
    {
        int maint = atoi(cmd + 10);
        thermo->maintain_speed = maint;
        snprintf(response, resp_len, "Maintain speed set: %d", maint);
    }
    else if (strcmp(cmd, "GET TEMP") == 0)
    {
        int cur_temp = thermostat_get_temperature(thermo);
        snprintf(response, resp_len, "Current temp: %d", cur_temp);
    }
    else if (strcmp(cmd, "GET STATUS") == 0)
    {
        snprintf(response, resp_len, "Temp=%d,Speed=%d,Heating=%d",
                 thermo->current_temp, thermo->current_speed, thermo->heating);
    }
    else
    {
        snprintf(response, resp_len, "Unknown command");
    }
}

void thermostat_task(void *param)
{
    thermostat_t *thermo = (thermostat_t *)param;
    if (!thermo)
        return;

    while (1)
    {
        thermostat_update(thermo);
        vTaskDelay(pdMS_TO_TICKS(1000)); // 每秒更新
    }
}

// void i2c_slave_task(void *arg)
// {
//     uint8_t data[16];
//     while (1) {
//         int len = i2c_slave_read_buffer(I2C_SLAVE_NUM, data, sizeof(data), pdMS_TO_TICKS(1000));
//         if (len > 0) {
//             ESP_LOGI("I2C_SLAVE", "Recv %d bytes: %02X %02X %02X", len, data[0], data[1], data[2]);
//         }
//     }
// }

void app_init_task(void)
{
    // 初始化从机
    // if (i2c_slave_init() != ESP_OK)
    // {
    //     ESP_LOGE(TAG_SLAVE, "Slave init failed, aborting tasks");
    //     return;
    // }
    my_i2c_device_handle_t my_dev;
    ESP_ERROR_CHECK(my_i2c_master_init(&my_dev));

    // 初始化恒温器结构体
    thermostat_init(&thermo, &dev,
                    38, // 目标温度
                    1,  // 温差容忍
                    3,  // 调温速度
                    2); // 恒温维持速度

    dev.dev = my_dev; // 关键绑定！

    // xTaskCreate(i2c_slave_task, "i2c_slave_task", 4096, NULL, 5, NULL);
    xTaskCreate(thermostat_task, "thermo_task", 2048, &thermo, 10, NULL);
    ESP_LOGI(TAG_MASTER, "App initialized (driver_ng)");
}
