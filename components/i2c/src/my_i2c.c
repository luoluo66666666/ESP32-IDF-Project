#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "device_reg.h"

#define TAG "I2C_LOOPBACK"

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
static void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA,
        .scl_io_num = I2C_MASTER_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };

    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&conf,&bus_handle));

    my_i2c_device_config_t my_device_config = {
        .i2c_dev_config_1.scl_speed_hz = I2C_FREQ_HZ,
        .i2c_dev_config_1.device_address = 0x50,
        .addr_bits = 3,
        .write_delay_ms = 5,
    };

    my_i2c_device_handle_t device_handle;
    
    ESP_ERROR_CHECK(my_i2c_device_init(bus_handle,&my_device_config,&device_handle));

    ESP_LOGI("I2C", "I2C 初始化完成，SCL=%d SDA=%d", I2C_MASTER_SCL, I2C_MASTER_SDA);
}


esp_err_t my_i2c_device_init(i2c_master_bus_handle_t bus_handle,
                             const my_i2c_device_config_t *cfg,
                             my_i2c_device_handle_t *device_handle)
{
    if (!bus_handle || !cfg || !device_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    my_i2c_device_handle_t dev = (my_i2c_device_handle_t)calloc(1, sizeof(*dev));
    if (!dev) return ESP_ERR_NO_MEM;

    // 配置 I2C 设备
    i2c_device_config_t i2c_conf = {
        .scl_speed_hz = cfg->i2c_dev_config_1.scl_speed_hz,
        .device_address = cfg->i2c_dev_config_1.device_address,
    };

    // 添加设备到总线
    ret = i2c_master_bus_add_device(bus_handle, &i2c_conf, &dev->i2c_dev_1);
    if (ret != ESP_OK) {
        free(dev);
        return ret;
    }

    // 保存协议参数
    dev->addr_bits = cfg->addr_bits;
    dev->data_bits = 8;               // 协议固定为 8bit
    dev->write_delay_ms = cfg->write_delay_ms;
    memset(dev->buf, 0, sizeof(dev->buf));

    *device_handle = dev;
    return ESP_OK;
}



/*******************************************************************************
****@brief: i2c从机机初始化
****@author: Luo
****@date: 2025-10-13 08:40:58
********************************************************************************/
static void i2c_slave_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_SLAVE,
        .sda_io_num = I2C_SLAVE_SDA,
        .scl_io_num = I2C_SLAVE_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en = 0,
        .slave.slave_addr = I2C_SLAVE_ADDR,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_SLAVE_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_SLAVE_NUM, conf.mode, 128, 128, 0));
}

// 主机任务：每秒发送一帧
static void i2c_master_task(void *arg)
{
    uint8_t data_send[3] = {0x12, 0x34, 0x56};

    while (1)
    {
        esp_err_t ret = i2c_master_write_to_device(I2C_MASTER_NUM, I2C_SLAVE_ADDR, data_send, sizeof(data_send), pdMS_TO_TICKS(100));
        if (ret == ESP_OK)
            ESP_LOGI(TAG, "Master sent: %02X %02X %02X", data_send[0], data_send[1], data_send[2]);
        else
            ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(ret));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 从机任务：持续接收
static void i2c_slave_task(void *arg)
{
    uint8_t data_recv[3];

    while (1)
    {
        int len = i2c_slave_read_buffer(I2C_SLAVE_NUM, data_recv, sizeof(data_recv), pdMS_TO_TICKS(1000));
        if (len > 0)
        {
            ESP_LOGI(TAG, "Slave recv (%d bytes): %02X %02X %02X", len, data_recv[0], data_recv[1], data_recv[2]);
        }
    }
}

void i2c_main(void)
{
    i2c_master_init();
    i2c_slave_init();

    xTaskCreate(i2c_master_task, "i2c_master_task", 2048, NULL, 5, NULL);
    xTaskCreate(i2c_slave_task, "i2c_slave_task", 2048, NULL, 5, NULL);
}

void i2c_eeprom_wait_idle(my_i2c_device_handle_t dev)
{
    // This is time for EEPROM Self-Timed Write Cycle
    vTaskDelay(pdMS_TO_TICKS(dev->write_delay_ms));
}
/*******************************************************************************
****@brief:
****@author: Luo
****@date: 2025-10-13 08:41:23
********************************************************************************/
esp_err_t i2c_master_read(my_i2c_device_handle_t dev,uint8_t addr,uint8_t data)
{
    uint32_t value = 0;

    value |= (0 << 19);              // bit19 = 0 写
    value |= ((addr & 0x7) << 16);   // bit18~16 地址
    value |= ((data & 0xFF) << 8);   // bit15~8 数据
    value |= ((data ^ 0xFF) & 0xFF); // bit7~0 校验码

    for (size_t i = 0; i < 3; i++)
    {
        dev->buf[i] = (value >> (8 * (2 - i))) & 0xFF;
    }

    uint32_t ret = i2c_master_transmit(dev->i2c_dev_1, dev->buf, sizeof(dev->buf), -1);


}
