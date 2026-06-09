#ifndef __MODE_CTRL_H__
#define __MODE_CTRL_H__

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_adc/adc_continuous.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


/*-----------------------------------------------------------
 *  DC宏定义
 *----------------------------------------------------------*/
#define READ_LEN            64          // 一次读取的转换数据长度
#define ADC_SAMPLE_FREQ_HZ  1000        // 采样频率 1kHz，可根据需要调整
#define ADC_CHANNEL         ADC_CHANNEL_0   // ADC1 通道 0
#define ADC_UNIT            ADC_UNIT_1       // ADC 单元
/*-----------------------------------------------------------
 *  控制数字输出引脚的辅助宏
 *----------------------------------------------------------*/
/**
 * @brief  将指定 DO 引脚置高电平（打开）
 * @param  pin 引脚索引（do_pin 数组中的下标）
 */
#define TURN_ON(pin) set_do_pin((pin), 1)

/**
 * @brief  将指定 DO 引脚置低电平（关闭）
 * @param  pin 引脚索引（do_pin 数组中的下标）
 */
#define TURN_OFF(pin) set_do_pin((pin), 0)

/**
 * @brief  翻转指定 DO 引脚的电平状态
 * @param  pin 引脚索引（do_pin 数组中的下标）
 */
#define TOGGLE(pin) set_do_pin((pin), !get_do_pin((pin)))

/**
 * @brief  批量打开指定范围内的 DO 引脚(包含 start 和 end)
 * \ 表示续行，告诉预处理器下一行仍是宏内容
 * @param  start 起始引脚索引
 * @param  end   结束引脚索引
 */
#define GROUP_ON(start, end)                         \
    do                                               \
    {                                                \
        for (size_t i = (start); i <= (end); ++i)    \
        {                                            \
            TURN_ON(i);                              \
        }                                            \
    } while (0)

/**
 * @brief  批量关闭指定范围内的 DO 引脚（包含 start 和 end）
 * \ 表示续行，告诉预处理器下一行仍是宏内容
 * @param  start 起始引脚索引
 * @param  end   结束引脚索引
 */
#define GROUP_OFF(start, end)                        \
    do                                               \
    {                                                \
        for (size_t i = (start); i <= (end); ++i)    \
        {                                            \
            TURN_OFF(i);                             \
        }                                            \
    } while (0)

/**
 * @brief  延时 1 秒（基于 FreeRTOS 的任务延时）
 * @note   vTaskDelay 会使当前任务进入阻塞状态，允许其他任务运行
 */
static inline void delay_1s(void)
{
    vTaskDelay(pdMS_TO_TICKS(1000)); // pdMS_TO_TICKS 宏将毫秒转换为 FreeRTOS 时钟节
}

static uint8_t last_di = 0;
static uint8_t cur_di = 0;

#define DO_PIN_NUM 28

/* 公用函数声明 */
extern int do_pin[DO_PIN_NUM];

esp_err_t pin_init(void);
esp_err_t sensor_init(void);

int set_do_pin(int index, int level);
int get_do_pin(int index);
int get_di_pin(int index);

void start_mode0(void);
int mode1(void);
int mode2(void);
int mode3(void);
int mode4(void);
int mode4_zhongyao_v2(void);
int mode5_up(void);
int mode5_down(void);
void test_task(void *pvParameters);
void start_mode_test(void);

/*-----------------------------------------------------------
 * DI 数字输入 — 可配置功能 + 后台监测任务
 *
 * 在 mode_ctrl.c 的 di_role_config[] 中按 DI 下标填入 di_role_t 数字：
 *   0 = 普通输入（仅缓存）
 *   1 = 报警（设备侧不暂停洗涤）
 *   2 = 暂停（按下保存 DO 并全关，松开后恢复）
 * TCP 主动推送统一为 DI,DIx=0|1，含义由上位机配置表判断。
 *
 * main 中 xTaskCreate(di_input_monitor_task, ...) 创建监测任务（100ms）。
 * 洗涤模式只读 di_get_cached_inputs() / di_is_pause_hold()，不直接采样 GPIO。
 *----------------------------------------------------------*/
#define DI_CHANNEL_COUNT 6

/** DI 通道功能（填入 di_role_config[] 的数字） */
typedef enum
{
    DI_ROLE_NONE  = 0, /**< 普通输入，仅更新缓存 */
    DI_ROLE_ALARM = 1, /**< 报警：设备侧不暂停洗涤 */
    DI_ROLE_PAUSE = 2, /**< 暂停：按住 DO 全关，松开恢复 */
} di_role_t;

/**
 * 各路 DI 功能配置（下标 = DI0～DI5）
 * 定义在 mode_ctrl.c，改表即可，无需改监测任务。
 */
extern const uint8_t di_role_config[DI_CHANNEL_COUNT];

/** DI 输入监测任务入口（main 里 xTaskCreate） */
void di_input_monitor_task(void *param);

/** 读取监测任务缓存的 6 路 DI 位图（bitN = DI N 为高） */
uint8_t di_get_cached_inputs(void);

/** 是否有配置为「暂停」的 DI 当前按下（洗涤可据此不推进计时） */
bool di_is_pause_hold(void);

/** 读取某路 DI 的功能配置数字 */
di_role_t di_get_channel_role(int channel);

/** @deprecated 仅供监测任务内部采样，外部请用 di_get_cached_inputs() */
uint8_t read_all_inputs(void);

/** 从 raw 中提取所有配置为「报警」的位 */
uint8_t di_alarm_extract(uint8_t raw_di);

/** 是否有任意报警位有效 */
bool di_alarm_any_active(uint8_t alarm_bits);

/** 计算报警上升沿：cur & ~last */
uint8_t di_alarm_rising_edge(uint8_t last_alarm, uint8_t cur_alarm);

/**
 * 格式化为协议 ERR 字段（仅含配置为报警的通道）
 * 例：DI,ERR,DI1 或 DI,ERR,DI0|DI,ERR,DI1
 */
int format_di_alarm_err(uint8_t raw_di, char *buf, size_t buf_len);

/** 格式化为 TCP 主动推送单行：DI,DIx=0|1 */
int format_di_push_line(int channel, int level, char *buf, size_t buf_len);

/** @deprecated 请改用 di_get_cached_inputs()；暂停由监测任务处理 */
uint8_t input_state_change_handler(void);

/*-----------------------------------------------------------
 * 运行期事件推送（由 Wifi 模块注册，用于 TCP 主动上报）
 *----------------------------------------------------------*/
typedef void (*mode_ctrl_event_push_fn)(const char *line);

/** 注册推送回调（如 wifi_module_tcp_push_line） */
void mode_ctrl_set_event_push_cb(mode_ctrl_event_push_fn fn);

/** 推送一行事件到云端 TCP；未注册回调时仅打日志 */
void mode_ctrl_push_event(const char *line);

#endif // __MODE_CTRL_H__
