#include "mode_ctrl.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"
#include <stdio.h>
#include <inttypes.h>
#include "gatt_svc.h"
// #include "../../../../../../ESP-IDF/v5.4.1/esp-idf/components/esp_adc/include/esp_adc/adc_cali.h"
#include <math.h>

extern QueueHandle_t ble_tx_queue;

/*
 * DO 映射表（软件下标 do_pin[0..27] ↔ 板级 MCU_DO_xx ↔ GPIO，共 DO_PIN_NUM 路）
 * 控制接口：TURN_ON(n) / TURN_OFF(n) / set_do_pin(n, level)，n 为下标，非丝印 DO 号。
 *
 * RS485（Modbus，自动收发 MAX3485，sdkconfig）：
 *   MCU_TXD = GPIO11，MCU_RXD = GPIO12（与 do_pin[18]/[17] 同脚，Modbus 初始化后会切 UART 功能）
 *
 * 其它：
 *   GPIO19 = 彩灯（mode4，不在本表）
 *   GPIO3/8  板级为 MCU_SPEAKER_STOP / MCU_SPEAKER_START
 */
int do_pin[DO_PIN_NUM] = {
    GPIO_NUM_1,  /* [0]  MCU_DO_0  */
    GPIO_NUM_2,  /* [1]  MCU_DO_1  */
    GPIO_NUM_42, /* [2]  MCU_DO_2  */
    GPIO_NUM_41, /* [3]  MCU_DO_3  */
    GPIO_NUM_40, /* [4]  MCU_DO_4  */
    GPIO_NUM_39, /* [5]  MCU_DO_5  */
    GPIO_NUM_38, /* [6]  MCU_DO_6  */
    GPIO_NUM_37, /* [7]  MCU_DO_7  */
    GPIO_NUM_36, /* [8]  MCU_DO_8  */
    GPIO_NUM_35, /* [9]  MCU_DO_9  */
    GPIO_NUM_0,  /* [10] MCU_DO_10 (strap，慎用) */
    GPIO_NUM_45, /* [11] MCU_DO_11 (strap，main 里常保持 ON) */
    GPIO_NUM_48, /* [12] MCU_DO_12 */
    GPIO_NUM_47, /* [13] MCU_DO_13 */
    GPIO_NUM_21, /* [14] MCU_DO_14 */
    GPIO_NUM_14, /* [15] MCU_DO_15 */
    GPIO_NUM_13, /* [16] MCU_DO_16 */
    GPIO_NUM_12, /* [17] MCU_RXD / Modbus RX，勿当继电器用 */
    GPIO_NUM_11, /* [18] MCU_TXD / Modbus TX，勿当继电器用 */
    GPIO_NUM_10, /* [19] MCU_DO_19 */
    GPIO_NUM_9,  /* [20] MCU_DO_20 */
    GPIO_NUM_46, /* [21] MCU_DO_21 (strap) */
    GPIO_NUM_3,  /* [22] MCU_SPEAKER_STOP */
    GPIO_NUM_8,  /* [23] MCU_SPEAKER_START */
    GPIO_NUM_18, /* [24] MCU_DO_24 */
    GPIO_NUM_17, /* [25] MCU_DO_25 */
    GPIO_NUM_20, /* [26] MCU_DO_26 */
    GPIO_NUM_19, /* [27] MCU_DO_27 */
};

/*
 * DI 映射表（软件下标 di_pin[0..5] ↔ 板级 MCU_DI_xx ↔ GPIO）
 * 读取：get_di_pin(n)；暂停键等为 di_pin[2]（MODE 里常用）。
 */
int di_pin[] = {
    GPIO_NUM_4,  /* [0] MCU_DI_0  跨循环锁定 */
    GPIO_NUM_5,  /* [1] MCU_DI_1  跨循环锁定 */
    GPIO_NUM_6,  /* [2] MCU_DI_2  暂停键 */
    GPIO_NUM_7,  /* [3] MCU_DI_3  流量脉冲（sensor_init 未启用时仅普通输入） */
    GPIO_NUM_15, /* [4] MCU_DI_4  水位等（mode_test） */
    GPIO_NUM_16, /* [5] MCU_DI_5  数字输入 */
};

static const char *TAG = "MODE_CTRL";
static const char *FLOW = "flow_sensor";

// ================== 全局定义 ==================
#define TAG "SENSOR"
#define IO_DEBUG_ENABLE 0

#if IO_DEBUG_ENABLE
#define IO_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define IO_LOGI(...)
#endif

#define VREF 3300
#define R_FIXED 50000.0f  // 分压电阻
#define ALPHA 0.2f        // 指数滤波系数
#define MOVING_AVG_LEN 10 // 滑动平均窗口大小

// ---------- 手动电压校准 ----------
#define V_CAL_MCU 1.54f                              // MCU 实测电压
#define V_CAL_REAL 1.60f                             // 万用表实际电压
#define V_CORRECTION_FACTOR (V_CAL_REAL / V_CAL_MCU) // 校准系数

// 流量传感器参数
volatile uint32_t flow_pulse_count = 0;
static uint64_t last_pulse_time_us = 0;
#define MIN_PULSE_INTERVAL_US 40000 // 40ms 防抖（25Hz以上有效）
#define PULSE_PER_L 660             // 每升水脉冲数
#define FLOW_TASK_PERIOD_MS 1000    // 每秒统计一次

// ADC 全局
static adc_continuous_handle_t adc_handle = NULL;
// static adc_cali_handle_t adc_cali_handle = NULL;

extern QueueHandle_t ble_tx_queue; // 由 BLE 模块提供
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

// NTC 转温度公式
float ntc_resistance_to_temp(float r_ntc)
{
    const float T0 = 298.15f;  // 25℃ = 298.15K
    const float R0 = 50000.0f; // 25℃ NTC 阻值
    const float B = 3950.0f;   // Beta 值

    float tempK = 1.0f / ((1.0f / T0) + (1.0f / B) * logf(r_ntc / R0));
    return tempK - 273.15f; // 转摄氏度
}

#define SEND_INTERVAL_MS 5000 // 5 秒
// // ================== 流量任务 ==================
// void sensor_task(void *pvParameters)
// {
//     uint32_t last_count = 0;
//     ble_data_t tx_data;
//     TickType_t last_send_tick = 0;
//     const TickType_t send_interval = pdMS_TO_TICKS(5000); // 5 秒发送一次
//     bool queue_full_flag = false; // 队列满标志

//     while (1)
//     {
//         vTaskDelay(pdMS_TO_TICKS(100));

//         if ((xTaskGetTickCount() - last_send_tick) < send_interval)
//             continue;

//         uint32_t count = flow_pulse_count;
//         uint32_t delta = count - last_count;
//         last_count = count;

//         float flow_l_min = (delta * 60.0f) / PULSE_PER_L;

//         int len = snprintf((char *)tx_data.buf, sizeof(tx_data.buf),
//                            "Pulse=%lu,Flow=%.2f", (unsigned long)delta, flow_l_min);
//         tx_data.len = len;

//         if (uxQueueSpacesAvailable(ble_tx_queue) > 0)
//         {
//             xQueueSend(ble_tx_queue, &tx_data, 0);
//             last_send_tick = xTaskGetTickCount();
//             queue_full_flag = false; // 队列有空，重置标志
//             ESP_LOGI(TAG, "Flow queued: %s", tx_data.buf);
//         }
//         else
//         {
//             if (!queue_full_flag)
//             {
//                 ESP_LOGW(TAG, "BLE TX queue full, flow data dropped");
//                 queue_full_flag = true; // 只打印一次
//             }
//         }
//     }
// }

// // ================== NTC 任务 ==================
// void ntc_task(void *pv)
// {
//     float v_filtered = 0.0f;
//     TickType_t last_send_tick = 0;
//     const TickType_t send_interval = pdMS_TO_TICKS(5000); // 5 秒发送一次
//     bool queue_full_flag = false;

//     while (1)
//     {
//         uint8_t result[READ_LEN];
//         uint32_t ret_num = 0;

//         if (adc_continuous_read(adc_handle, result, READ_LEN, &ret_num, 1000) == ESP_OK)
//         {
//             adc_digi_output_data_t *p = (adc_digi_output_data_t *)result;
//             uint32_t adc_raw = p->type2.data;

//             int voltage = 0;
//             if (adc_cali_handle)
//                 adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage);
//             else
//                 voltage = (adc_raw * VREF) / 4095;

//             float v = voltage / 1000.0f;
//             if (v_filtered == 0.0f) v_filtered = v;
//             v_filtered = ALPHA * v + (1 - ALPHA) * v_filtered;

//             float v_corrected = v_filtered * V_CORRECTION_FACTOR;
//             float r_ntc = (R_FIXED * v_corrected) / (3.3f - v_corrected);
//             float tempC = ntc_resistance_to_temp(r_ntc);

//             if ((xTaskGetTickCount() - last_send_tick) >= send_interval)
//             {
//                 if (uxQueueSpacesAvailable(ble_tx_queue) > 0)
//                 {
//                     ble_data_t tx_data;
//                     int len = snprintf((char *)tx_data.buf, sizeof(tx_data.buf), "TEMP=%.2f", tempC);
//                     tx_data.len = len;

//                     xQueueSend(ble_tx_queue, &tx_data, 0);
//                     last_send_tick = xTaskGetTickCount();
//                     queue_full_flag = false; // 队列有空，重置标志

//                     ESP_LOGI(TAG, "NTC queued: raw=%" PRIu32 ", Vcorr=%.3fV, T=%.2f°C",
//                              adc_raw, v_corrected, tempC);
//                 }
//                 else
//                 {
//                     if (!queue_full_flag)
//                     {
//                         ESP_LOGW(TAG, "BLE TX queue full, temp dropped");
//                         queue_full_flag = true; // 只打印一次
//                     }
//                 }
//             }
//         }

//         vTaskDelay(pdMS_TO_TICKS(100));
//     }
// }

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
// esp_err_t sensor_init(void)
// {
//     esp_err_t ret = ESP_OK;

//     // === ADC 初始化 (NTC) ===
//     adc_continuous_handle_cfg_t adc_cfg = {
//         .max_store_buf_size = 1024,
//         .conv_frame_size = READ_LEN,
//     };
//     ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_cfg, &adc_handle));

//     adc_continuous_config_t dig_cfg = {
//         .sample_freq_hz = 1000,
//         .conv_mode = ADC_CONV_SINGLE_UNIT_1,
//         .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
//     };

//     adc_digi_pattern_config_t adc_pattern = {
//         .atten = ADC_ATTEN_DB_12,
//         .channel = ADC_CHANNEL_4,     // ADC1_CHANNEL_4 →5 (ESP32-S3))
//         .unit = ADC_UNIT_1,
//         .bit_width = ADC_BITWIDTH_12,
//     };

//     dig_cfg.pattern_num = 1;
//     dig_cfg.adc_pattern = &adc_pattern;

//     ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));
//     ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

//     ESP_LOGI(TAG, "ADC continuous sampling started on unit %d, channel %d (GPIO%d)",
//              adc_pattern.unit, adc_pattern.channel, di_pin[2]);

//     // === 流量传感器 GPIO 初始化 ===
//     gpio_config_t io_conf = {
//         .intr_type = GPIO_INTR_POSEDGE,
//         .mode = GPIO_MODE_INPUT,
//         .pin_bit_mask = 1ULL << di_pin[3],
//         .pull_up_en = GPIO_PULLUP_ENABLE,
//     };
//     gpio_config(&io_conf);
//     ESP_LOGI(TAG, "Flow sensor input initialized on pin %d", di_pin[3]);

//     gpio_install_isr_service(0);
//     gpio_isr_handler_add(di_pin[3], flow_isr_handler, NULL);

//     // === 创建任务 ===
//     xTaskCreate(sensor_task, "flow_sensor_task", 4096, NULL, 5, NULL);
//     xTaskCreate(ntc_task, "ntc_task", 4096, NULL, 5, NULL);

//     return ret;
// }

/*******************************************************************************
****@brief: 设置指定 DO 引脚的输出电平
****@param: index:DO引脚数组中的索引
****@param: level:输出电平，通常为 0 或 1
****@author: Luo
****@date: 2025-08-08 14:46:06
********************************************************************************/
int set_do_pin(int index, int level)
{
    if (index < 0 || index >= (int)(sizeof(do_pin) / sizeof(do_pin[0])))
    {
        ESP_LOGE(TAG, "set_do_pin: Index out of bounds: %d", index);
        return -1;
    }

    if (level != 0 && level != 1)
    {
        ESP_LOGW(TAG, "set_do_pin: Invalid level %d, forcing to 0 or 1", level);
        level = (level != 0) ? 1 : 0;
    }

    esp_err_t ret = gpio_set_level(do_pin[index], level);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "set_do_pin: Failed to set DO pin %d to level %d: %s",
                 do_pin[index], level, esp_err_to_name(ret));
        return ret; // 返回错误码
    }

    // 4. 打印GPIO开启的日志
    IO_LOGI("set_do_pin: Set DO%d (GPIO%d) to level %d", index, do_pin[index], level);
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
    if (index < 0 || index >= (int)(sizeof(do_pin) / sizeof(do_pin[0])))
    {
        ESP_LOGE(TAG, "get_do_pin: Index out of bounds: %d", index);
        return -1;
    }
    int level = get_output_reg_level(do_pin[index]);

    IO_LOGI("get_do_pin: Returning DO%d (GPIO%d), level: %d", index, do_pin[index], level);
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

    IO_LOGI("get_di_pin: Returning DI%d (GPIO%d), level: %d", index, di_pin[index], level);
    return level;
}

#define INPUT_NUM (sizeof(di_pin) / sizeof(di_pin[0]))
#define STABLE_COUNT 3 // 稳定判断次数
uint8_t read_all_inputs(void)
{
    uint8_t count[INPUT_NUM] = {0}; // 每路计数
    uint8_t result = 0;
    int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};

    // 连续读取 STABLE_COUNT 次
    for (int n = 0; n < STABLE_COUNT; n++)
    {
        for (int i = 0; i < INPUT_NUM; i++)
        {
            if (get_di_pin(i)) // 第 i 路为高
                count[i]++;
        }
    }
    // 判断每一路是否稳定
    for (int i = 0; i < INPUT_NUM; i++)
    {
        if (count[i] == STABLE_COUNT)
            result |= (1 << i); // 连续高 → 置 1
        // 否则保持为 0
    }

    return result; // 返回八位，每位对应一路输入
}

uint8_t input_state_change_handler(void)
{

    int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};
    // 读取当前稳定输入状态
    cur_di = read_all_inputs();
    last_di = cur_di;

    // 暂停键 DI2 检测
    if (cur_di & (1 << 2)) // 注意这里用 cur_di 替代未定义的 result
    {
        ESP_LOGI(TAG, "Paused");

        // 保存 DO 状态并关闭
        for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); ++i)
        {
            do_level[i] = get_do_pin(i);
            TURN_OFF(i);
        }

        // 等待按键松开（实时更新 cur_di）
        do
        {
            cur_di = read_all_inputs();
            vTaskDelay(pdMS_TO_TICKS(100)); // 每 100 ms 检查一次
        } while (cur_di & (1 << 2));

        // 恢复 DO 状态
        for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); ++i)
        {
            set_do_pin(i, do_level[i]);
        }

        ESP_LOGI(TAG, "Resumed");
    }

    return cur_di;

}

/**
 * @brief 判断跨循环锁定位（DI0~DI2）
 * @return 1 表示任意锁定位为1，本次任务不允许执行
 *         0 表示允许执行
 */
uint8_t check_cross_loop_lock(void)
{
    if (cur_di & (1 << 0)) { ESP_LOGI(TAG, "DI0 锁定"); return 1; }
    if (cur_di & (1 << 1)) { ESP_LOGI(TAG, "DI1 锁定"); return 1; }
    if (cur_di & (1 << 2)) { ESP_LOGI(TAG, "DI2 锁定"); return 1; }

    return 0; // 没有锁定，允许执行

}
