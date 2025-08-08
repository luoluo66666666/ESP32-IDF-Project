#ifndef __MODE_CTRL_H__
#define __MODE_CTRL_H__

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

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

esp_err_t pin_init(void);
int set_do_pin(int index, int level);
int get_do_pin(int index);
int get_di_pin(int index);

int mode0(void);
int mode1(void);
int mode2(void);
int mode3(void);
int mode4(void);
int mode5_up(void);
int mode5_down(void);



#endif // __MODE_CTRL_H__