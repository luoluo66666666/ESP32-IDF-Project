#include "mode_ctrl.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"
#include <stdio.h>

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

/*******************************************************************************
****@brief 初始化所有 DO（输出）和 DI（输入）引脚
* 1. 先遍历 DO 引脚数组，依次复位引脚并设置为输出模式
* 2. 再遍历 DI 引脚数组，依次复位引脚、设置为输入模式并开启上
****@author: Luo
****@date: 2025-08-08 14:39:09
********************************************************************************/
esp_err_t pin_init(void)
{
    esp_err_t ret = ESP_OK; // 保存每一步操作的返回状态

    /* ---------- 初始化所有 DO（数字输出）引脚 ---------- */
    for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); i++)
    {
        // 1. 将引脚切换为 GPIO 模式，释放所有外设复用
        esp_rom_gpio_pad_select_gpio(do_pin[i]);

        // 2. 复位引脚为默认状态（禁用所有功能，恢复为普通GPIO）
        ret = gpio_reset_pin(do_pin[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset pin %d: %s", do_pin[i], esp_err_to_name(ret));
            return ret; // 如果复位失败，直接返回错误
        }

        // 3. 设置引脚方向为输出
        ret = gpio_set_direction(do_pin[i], GPIO_MODE_OUTPUT);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set direction for pin %d: %s", do_pin[i], esp_err_to_name(ret));
            return ret; // 设置方向失败，返回错误
        }

        // 4. 设置引脚初始输出电平为低，防止悬空
        ret = gpio_set_level(do_pin[i], 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set level for pin %d: %s", do_pin[i], esp_err_to_name(ret));
            return ret; // 设置电平失败，返回错误
        }
    }

    /* ---------- 初始化所有 DI（数字输入）引脚 ---------- */
    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); i++)
    {
        // 1. 切换引脚为 GPIO 模式，释放复用
        esp_rom_gpio_pad_select_gpio(di_pin[i]);

        // 2. 复位引脚为默认状态
        ret = gpio_reset_pin(di_pin[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset pin %d: %s", di_pin[i], esp_err_to_name(ret));
            return ret; // 失败则返回
        }

        // 3. 设置引脚方向为输入
        ret = gpio_set_direction(di_pin[i], GPIO_MODE_INPUT);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set direction for pin %d: %s", di_pin[i], esp_err_to_name(ret));
            return ret;
        }

        // 4. 设置内部上拉电阻，防止输入悬空产生干扰
        ret = gpio_set_pull_mode(di_pin[i], GPIO_PULLUP_ENABLE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set pull mode for pin %d: %s", di_pin[i], esp_err_to_name(ret));
            return ret;
        }
    }

    return ret; // 所有引脚成功初始化返回 ESP_OK
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





