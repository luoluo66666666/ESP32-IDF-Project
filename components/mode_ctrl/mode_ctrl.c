#include "mode_ctrl.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"
#include <stdio.h>
#include <inttypes.h>
#include "gatt_svc.h"
#include "../../../../../../ESP-IDF/v5.4.1/esp-idf/components/esp_adc/include/esp_adc/adc_cali.h"
#include <math.h>


extern QueueHandle_t ble_tx_queue;

int do_pin[] = {
    GPIO_NUM_1,
    GPIO_NUM_2,
    GPIO_NUM_42,
    GPIO_NUM_41,
    GPIO_NUM_40,
    GPIO_NUM_39,
    GPIO_NUM_38,
    GPIO_NUM_37,
    GPIO_NUM_36,
    GPIO_NUM_35,
    GPIO_NUM_0,
    GPIO_NUM_45,
    GPIO_NUM_48,
    GPIO_NUM_47,
    GPIO_NUM_21,
    GPIO_NUM_14,
    GPIO_NUM_13,
    GPIO_NUM_12,
    GPIO_NUM_11,
    GPIO_NUM_10,
    GPIO_NUM_9,
    GPIO_NUM_46,
    GPIO_NUM_3,
    GPIO_NUM_8,
    GPIO_NUM_18,
    GPIO_NUM_17,
};

int di_pin[] = {
    GPIO_NUM_4,
    GPIO_NUM_5,
    GPIO_NUM_6,
    GPIO_NUM_7,
    GPIO_NUM_15,
    GPIO_NUM_16,
};

static const char *TAG = "MODE_CTRL";
static const char *FLOW = "flow_sensor";

// ================== 全局定义 ==================
#define TAG "SENSOR"
#define VREF 3300
#define R_FIXED 50000.0f // 分压电阻
#define ALPHA   0.2f        // 指数滤波系数
#define MOVING_AVG_LEN 10          // 滑动平均窗口大小


// ---------- 手动电压校准 ----------
#define V_CAL_MCU   1.54f   // MCU 实测电压
#define V_CAL_REAL  1.60f   // 万用表实际电压
#define V_CORRECTION_FACTOR (V_CAL_REAL / V_CAL_MCU)  // 校准系数

// 流量传感器参数
volatile uint32_t flow_pulse_count = 0;
static uint64_t last_pulse_time_us = 0;
#define MIN_PULSE_INTERVAL_US 40000   // 40ms 防抖（25Hz以上有效）
#define PULSE_PER_L 660               // 每升水脉冲数
#define FLOW_TASK_PERIOD_MS 1000      // 每秒统计一次

// ADC 全局
static adc_continuous_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;


extern QueueHandle_t ble_tx_queue;   // 由 BLE 模块提供
extern uint16_t custom_chr_conn_handle;
extern bool custom_notify_enabled;


// ================== 流量计中断 ==================
static void IRAM_ATTR flow_isr_handler(void *arg)
{
    uint64_t now = esp_timer_get_time();
    if (now - last_pulse_time_us > MIN_PULSE_INTERVAL_US)
    {
        flow_pulse_count++;
        last_pulse_time_us = now;
    }
}

// ================== 流量任务 ==================
void sensor_task(void *pvParameters)
{
    uint32_t last_count = 0;
    ble_data_t tx_data;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(FLOW_TASK_PERIOD_MS));

        uint32_t count = flow_pulse_count;
        uint32_t delta = count - last_count;
        last_count = count;

        float flow_l_min = (delta * 60.0f) / PULSE_PER_L;

        // 构造数据
        int len = snprintf((char *)tx_data.buf, sizeof(tx_data.buf),
                           "Pulse=%lu,Flow=%.2f", (unsigned long)delta, flow_l_min);
        tx_data.len = len;

        // 放入队列（给 BLE 任务）
        if (xQueueSend(ble_tx_queue, &tx_data, 0) != pdPASS)
        {
            ESP_LOGW(TAG, "BLE TX queue full, data dropped");
        }
        else
        {
            ESP_LOGI(TAG, "Flow queued: %s", tx_data.buf);
        }
    }
}


// NTC 转温度公式
float ntc_resistance_to_temp(float r_ntc)
{
    const float T0 = 298.15f;  // 25℃ = 298.15K
    const float R0 = 50000.0f; // 25℃ NTC 阻值
    const float B  = 3950.0f;  // Beta 值

    float tempK = 1.0f / ((1.0f / T0) + (1.0f / B) * logf(r_ntc / R0));
    return tempK - 273.15f; // 转摄氏度
}

void ntc_task(void *pv)
{
    float v_filtered = 0.0f;

    while (1)
    {
        uint8_t result[READ_LEN];
        uint32_t ret_num = 0;

        if (adc_continuous_read(adc_handle, result, READ_LEN, &ret_num, 1000) == ESP_OK)
        {
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)result;
            uint32_t adc_raw = p->type2.data;

            int voltage = 0;
            if (adc_cali_handle)
            {
                adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage);
            }
            else
            {
                voltage = (adc_raw * VREF) / 4095;
            }

            float v = voltage / 1000.0f;

            // 指数滤波
            if (v_filtered == 0.0f) v_filtered = v;
            v_filtered = ALPHA * v + (1 - ALPHA) * v_filtered;

            // 电压校准
            float v_corrected = v_filtered * V_CORRECTION_FACTOR;

            float r_ntc = (R_FIXED * v_corrected) / (3.3f - v_corrected);
            float tempC = ntc_resistance_to_temp(r_ntc);

            ESP_LOGI(TAG, "NTC: raw=%" PRIu32 ", V=%.3fV, Vcorr=%.3fV, R=%.1fΩ, T=%.2f°C",
                     adc_raw, v_filtered, v_corrected, r_ntc, tempC);

            // BLE发送
            ble_data_t tx_data;
            int len = snprintf((char *)tx_data.buf, sizeof(tx_data.buf), "TEMP=%.2f", tempC);
            tx_data.len = len;

            if (xQueueSend(ble_tx_queue, &tx_data, 0) != pdPASS)
            {
                ESP_LOGW(TAG, "BLE TX queue full, temp dropped");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
/*******************************************************************************
****@brief 初始化所有 DO（输出）和 DI（输入）引脚 
* 1. 先遍历 DO 引脚数组，依次复位引脚并设置为输出模式
* 2. 再遍历 DI 引脚数组，依次复位引脚、设置为输入模式并开启上
* 3. ADC连续采样，设置ADC1通道5对应GPIO6(di_pin[2])
* 4. 采用中断检测水流计脉冲信号 di_pin[3]
****@author: Luo
****@date: 2025-08-08 14:39:09
********************************************************************************/
esp_err_t pin_init(void)
{
    esp_err_t ret = ESP_OK;

    // DO 初始化
    for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); i++)
    {
        esp_rom_gpio_pad_select_gpio(do_pin[i]);
        ESP_ERROR_CHECK(gpio_reset_pin(do_pin[i]));
        ESP_ERROR_CHECK(gpio_set_direction(do_pin[i], GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(do_pin[i], 0));
    }

    // DI 初始化
    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); i++)
    {
        esp_rom_gpio_pad_select_gpio(di_pin[i]);
        ESP_ERROR_CHECK(gpio_reset_pin(di_pin[i]));
        ESP_ERROR_CHECK(gpio_set_direction(di_pin[i], GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_set_pull_mode(di_pin[i], GPIO_PULLUP_ENABLE));
    }

    return ret;
}

// ================== 传感器初始化 ==================
esp_err_t sensor_init(void)
{
    esp_err_t ret = ESP_OK;

    // === ADC 初始化 (NTC) ===
    adc_continuous_handle_cfg_t adc_cfg = {
        .max_store_buf_size = 1024,
        .conv_frame_size = READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_cfg, &adc_handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 1000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };

    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = ADC_CHANNEL_4,     // ADC1_CHANNEL_4 →5 (ESP32-S3))
        .unit = ADC_UNIT_1,
        .bit_width = ADC_BITWIDTH_12,
    };

    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = &adc_pattern;

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));
    ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

    ESP_LOGI(TAG, "ADC continuous sampling started on unit %d, channel %d (GPIO%d)",
             adc_pattern.unit, adc_pattern.channel, di_pin[2]);

    // === 流量传感器 GPIO 初始化 ===
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << di_pin[3],
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "Flow sensor input initialized on pin %d", di_pin[3]);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(di_pin[3], flow_isr_handler, NULL);

    // === 创建任务 ===
    xTaskCreate(sensor_task, "flow_sensor_task", 4096, NULL, 5, NULL);
    xTaskCreate(ntc_task, "ntc_task", 4096, NULL, 5, NULL);

    return ret;
}

/*******************************************************************************
****@brief: 设置指定 DO 引脚的输出电平
****@param: index:DO引脚数组中的索引
****@param: level:输出电平，通常为 0 或 1
****@author: Luo
****@date: 2025-08-08 14:46:06
********************************************************************************/
int set_do_pin(int index, int level)
{
    // 1. 校验索引范围是否合法
    if (index < 0 || index >= (int)(sizeof(do_pin) / sizeof(do_pin[0])))
    {
        ESP_LOGE(TAG, "set_do_pin: Index out of bounds: %d", index);
        return -1; // 非法索引，返回错误
    }

    // 2. 限制 level 值为 0 或 1，防止传入错误电平
    if (level != 0 && level != 1)
    {
        ESP_LOGW(TAG, "set_do_pin: Invalid level %d, forcing to 0 or 1", level);
        level = (level != 0) ? 1 : 0;
    }

    // 3. 设置 GPIO 输出电平
    esp_err_t ret = gpio_set_level(do_pin[index], level);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "set_do_pin: Failed to set DO pin %d to level %d: %s",
                 do_pin[index], level, esp_err_to_name(ret));
        return ret; // 返回错误码
    }

    // 4. 打印GPIO开启的日志
    ESP_LOGI(TAG, "set_do_pin: Set DO%d (GPIO%d) to level %d", index, do_pin[index], level);
    return 0; // 成功返回
}

/*******************************************************************************
****@brief: 读取指定 GPIO 输出寄存器当前输出电平
*   该函数直接读取 GPIO_OUT_REG 寄存器的输出值位，不受输入电平影响。
*   只适用于普通 GPIO 引脚，某些特殊功能或外设占用的引脚可能不准
****@param: gpio_num:
****@author: Luo
****@date: 2025-08-08 14:46:47
********************************************************************************/
uint32_t get_output_reg_level(gpio_num_t gpio_num)
{
    // 读取 GPIO 输出寄存器的当前值
    uint32_t reg_value = REG_READ(GPIO_OUT_REG);

    // 通过位移和掩码取得对应引脚的输出电平 (0 或 1)
    return (reg_value >> gpio_num) & 0x1;
}

/*******************************************************************************
****@brief: 获取指定 DO（数字输出）引脚当前输出电平
****@param: index:DO 引脚数组中的索引
****@author: Luo
****@date: 2025-08-08 14:54:43
********************************************************************************/
int get_do_pin(int index)
{
    // 检查索引范围，避免越界访问数组
    if (index < 0 || index >= (int)(sizeof(do_pin) / sizeof(do_pin[0])))
    {
        ESP_LOGE(TAG, "get_do_pin: Index out of bounds: %d", index);
        return -1; // 无效索引返回错误码
    }
    // 读取对应 GPIO 引脚的输出寄存器电平
    int level = get_output_reg_level(do_pin[index]);

    ESP_LOGI(TAG, "get_do_pin: Returning DO%d (GPIO%d), level: %d", index, do_pin[index], level);
    return level;
}

/*******************************************************************************
****@brief: 获取指定 DI（数字输入）引脚当前输入电平
****@param: index:DI 引脚数组中的索引
****@author: Luo
****@date: 2025-08-08 14:55:07
********************************************************************************/
int get_di_pin(int index)
{
    // 检查索引范围，避免越界访问数组
    if (index < 0 || index >= (int)(sizeof(di_pin) / sizeof(di_pin[0])))
    {
        ESP_LOGE(TAG, "get_di_pin: Index out of bounds: %d", index);
        return -1; // 无效索引返回错误码
    }
    // 读取对应 GPIO 引脚的输入电平
    int level = gpio_get_level(di_pin[index]);

    ESP_LOGI(TAG, "get_di_pin: Returning DI%d (GPIO%d), level: %d", index, di_pin[index], level);
    return level;
}
