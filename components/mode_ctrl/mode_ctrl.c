#include "mode_ctrl.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
 * DI 硬件映射（软件下标 di_pin[0..5] ↔ MCU_DI_xx ↔ GPIO）
 * 各路「报警 / 暂停 / 普通」由下方 di_role_config[] 配置，与此表独立。
 */
int di_pin[] = {
    GPIO_NUM_4,  /* [0] MCU_DI_0 */
    GPIO_NUM_5,  /* [1] MCU_DI_1 */
    GPIO_NUM_6,  /* [2] MCU_DI_2 */
    GPIO_NUM_7,  /* [3] MCU_DI_3 流量脉冲 */
    GPIO_NUM_15, /* [4] MCU_DI_4 水位等 */
    GPIO_NUM_16, /* [5] MCU_DI_5 */
};

/*-----------------------------------------------------------
 * DI 功能配置表 — 按通道下标填入 di_role_t 数字
 *
 * 0 = DI_ROLE_NONE   普通输入
 * 1 = DI_ROLE_ALARM  报警（设备侧不暂停洗涤；TCP 推送见 format_di_push_line）
 * 2 = DI_ROLE_PAUSE  暂停（按住关 DO，松开恢复；TCP 推送见 format_di_push_line）
 *----------------------------------------------------------*/
const uint8_t di_role_config[DI_CHANNEL_COUNT] = {
    DI_ROLE_ALARM, /* [0] DI0 */
    DI_ROLE_ALARM, /* [1] DI1 */
    DI_ROLE_PAUSE, /* [2] DI2 暂停键 */
    DI_ROLE_ALARM,  /* [3] DI3 */
    DI_ROLE_NONE,  /* [4] DI4 */
    DI_ROLE_NONE,  /* [5] DI5 */
};

static const char *TAG = "MODE_CTRL";

// ================== 全局定义 ==================
#define IO_DEBUG_ENABLE 0

#if IO_DEBUG_ENABLE
#define IO_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define IO_LOGI(...)
#endif

// ADC 全局
static adc_continuous_handle_t adc_handle = NULL;
// static adc_cali_handle_t adc_cali_handle = NULL;

extern QueueHandle_t ble_tx_queue; // 由 BLE 模块提供
extern uint16_t custom_chr_conn_handle;
extern bool custom_notify_enabled;

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
#define STABLE_COUNT 3 /**< 连续采样次数，全部为高才认为 DI 有效 */

/**
 * @brief 读取全部 DI 并去抖
 * @return 位图，bitN=1 表示 DI N 稳定为高电平
 */
uint8_t read_all_inputs(void)
{
    uint8_t count[INPUT_NUM] = {0};
    uint8_t result = 0;

    for (int n = 0; n < STABLE_COUNT; n++)
    {
        for (int i = 0; i < INPUT_NUM; i++)
        {
            if (get_di_pin(i))
            {
                count[i]++;
            }
        }
    }

    for (int i = 0; i < INPUT_NUM; i++)
    {
        if (count[i] == STABLE_COUNT)
        {
            result |= (uint8_t)(1u << i);
        }
    }

    return result;
}

/** 生成某角色的通道位掩码（内部根据 di_role_config[] 计算） */
static uint8_t di_role_bitmask(di_role_t role)
{
    uint8_t mask = 0;

    for (int i = 0; i < DI_CHANNEL_COUNT; i++)
    {
        if (di_role_config[i] == (uint8_t)role)
        {
            mask |= (uint8_t)(1u << i);
        }
    }

    return mask;
}

di_role_t di_get_channel_role(int channel)
{
    if (channel < 0 || channel >= DI_CHANNEL_COUNT)
    {
        return DI_ROLE_NONE;
    }

    return (di_role_t)di_role_config[channel];
}

uint8_t di_alarm_extract(uint8_t raw_di)
{
    return (uint8_t)(raw_di & di_role_bitmask(DI_ROLE_ALARM));
}

bool di_alarm_any_active(uint8_t alarm_bits)
{
    return alarm_bits != 0;
}

uint8_t di_alarm_rising_edge(uint8_t last_alarm, uint8_t cur_alarm)
{
    return (uint8_t)(cur_alarm & (uint8_t)~last_alarm);
}

/** 格式化为 TCP 主动推送单行：DI,DIx=0|1（电平变化时推送，含义由上位机判断） */
int format_di_push_line(int channel, int level, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0)
    {
        return -1;
    }

    if (channel < 0 || channel >= DI_CHANNEL_COUNT)
    {
        return -1;
    }

    if (level != 0 && level != 1)
    {
        return -1;
    }

    snprintf(buf, buf_len, "DI,DI%d=%d", channel, level);
    return 0;
}

/* 格式化为 CMD:DI_GET / CMD:MODE_GET 的 ERR 字段（仅报警角色 DI） */
int format_di_alarm_err(uint8_t raw_di, char *buf, size_t buf_len)
{
    size_t pos = 0;

    if (buf == NULL || buf_len == 0)
    {
        return 0;
    }

    buf[0] = '\0';

    for (int i = 0; i < DI_CHANNEL_COUNT; i++)
    {
        if (di_role_config[i] != DI_ROLE_ALARM)
        {
            continue;
        }

        if (raw_di & (1u << i))
        {
            pos += (size_t)snprintf(buf + pos, buf_len - pos, "%sDI,ERR,DI%d", pos > 0 ? "|" : "", i);
            if (pos >= buf_len)
            {
                break;
            }
        }
    }

    return pos > 0 ? 1 : 0;
}

static volatile uint8_t s_di_cached_raw = 0; /* 监测任务缓存的 6 路 DI 位图 */
static volatile bool s_di_pause_hold   = false; /* 暂停键按住时为 true */

/* 供洗涤模式 / CMD:DI_GET 读取 DI 缓存 */
uint8_t di_get_cached_inputs(void)
{
    return s_di_cached_raw;
}

/* 暂停键是否按住（洗涤流程可据此不推进计时） */
bool di_is_pause_hold(void)
{
    return s_di_pause_hold;
}

/** 是否有任意配置为「暂停」的 DI 当前为高 */
static bool di_any_pause_pressed(uint8_t raw)
{
    uint8_t pause_mask = di_role_bitmask(DI_ROLE_PAUSE);

    return (raw & pause_mask) != 0;
}

/**
 * 暂停处理：由监测任务调用
 * 按下 → 保存 DO 并全关；按住 → 保持全关；松开 → 恢复 DO
 */
static void di_pause_service(uint8_t raw)
{
    static bool in_pause          = false;
    static int saved_do[DO_PIN_NUM] = {0};
    bool pressed                  = di_any_pause_pressed(raw);

    if (pressed)
    {
        if (!in_pause)
        {
            for (size_t i = 0; i < DO_PIN_NUM; i++)
            {
                saved_do[i] = get_do_pin((int)i);
            }

            in_pause         = true;
            s_di_pause_hold  = true;
            ESP_LOGI(TAG, "DI pause pressed, holding outputs off");
        }

        for (size_t i = 0; i < DO_PIN_NUM; i++)
        {
            TURN_OFF((int)i);
        }
    }
    else if (in_pause)
    {
        for (size_t i = 0; i < DO_PIN_NUM; i++)
        {
            set_do_pin((int)i, saved_do[i]);
        }

        in_pause        = false;
        s_di_pause_hold = false;
        ESP_LOGI(TAG, "DI pause released, outputs restored");
    }
}

/**
 * DI 输入监测任务（main 中 xTaskCreate 创建）
 * 1. 采样去抖并写缓存
 * 2. 按 di_role_config[] 处理暂停
 * 3. 任一路 DI 电平变化时推送 DI,DIx=0|1（上位机自行判断含义）
 */
void di_input_monitor_task(void *param)
{
    uint8_t last_raw = 0;
    char line[16]    = {0};

    (void)param;
    ESP_LOGI(TAG, "DI monitor started (roles from di_role_config[])");

    while (1)
    {
        uint8_t raw     = read_all_inputs();       /* 6 路 DI 采样（含去抖） */
        uint8_t changed = (uint8_t)(raw ^ last_raw); /* 与上次比，找出变化的位 */

        s_di_cached_raw = raw; /* 写缓存，供 CMD:DI_GET / 洗涤模式读取 */
        cur_di          = raw;
        last_di         = raw;

        di_pause_service(raw); /* 暂停键：按住关 DO，松开恢复 */

        for (int i = 0; i < DI_CHANNEL_COUNT; i++)
        {
            if (!(changed & (1u << i)))
            {
                continue;
            }

            int level = (raw & (1u << i)) ? 1 : 0;
            if (format_di_push_line(i, level, line, sizeof(line)) == 0)
            {
                mode_ctrl_push_event(line); /* 推 DI,DIx=0|1 到云端 TCP */
                ESP_LOGI(TAG, "DI push: %s", line);
            }
        }

        last_raw = raw;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


static mode_ctrl_event_push_fn s_event_push_fn = NULL; /* DI 等事件 TCP 推送回调 */

/* 注册推送回调（wifi_tcp_start 中绑定 wifi_module_tcp_push_line） */
void mode_ctrl_set_event_push_cb(mode_ctrl_event_push_fn fn)
{
    s_event_push_fn = fn;
}

/* 推送一行到云端 TCP（DI,DIx=n 等）；未注册回调时仅打日志 */
void mode_ctrl_push_event(const char *line)
{
    if (line == NULL || line[0] == '\0')
    {
        return;
    }

    if (s_event_push_fn != NULL)
    {
        s_event_push_fn(line);
    }
    else
    {
        ESP_LOGW(TAG, "event (no push cb): %s", line);
    }
}
