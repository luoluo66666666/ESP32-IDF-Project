// #include "ctrl_protocol.h"
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "mode_ctrl.h" // 引入模式控制相关函数的头文件

#define RUN_BIT BIT10        // 运行状态标志位
#define FAULT_BIT BIT11      // 故障状态标志位

// 定义各模式对应的事件位
#define MODE0_BIT BIT0
#define MODE1_BIT BIT1
#define MODE2_BIT BIT2
#define MODE3_BIT BIT3
#define MODE4_BIT BIT4
#define MODE5_UPPER_BIT BIT5
#define MODE5_LOWER_BIT BIT6
#define MODE_CLEAR BIT7

static EventGroupHandle_t e_ctrl_protocol; // 事件组句柄，用于管理运行/故障/模式状态

const char *TAG = "CTRL_PROTOCOL"; // 日志TAG

/**
 * @description: 函数描述
 * @Author: Luo
 * @Date: 2025-07-21 10:05:28
 * @LastEditTime: 
 * @LastEditors: Luo
 */
void ctrl_protocol_init(void)
{
    e_ctrl_protocol = xEventGroupCreate(); // 创建事件组
    if (e_ctrl_protocol == NULL)
    {
        ESP_LOGE(TAG, "Failed to create event group"); // 创建失败时打印错误
    }
    xEventGroupSetBits(e_ctrl_protocol, MODE0_BIT); // 默认设置为MODE0模式

    // 清除其他模式相关位，确保只有MODE0被置位
    xEventGroupClearBits(e_ctrl_protocol, MODE1_BIT |
                                              MODE2_BIT |
                                              MODE3_BIT |
                                              MODE4_BIT |
                                              MODE5_UPPER_BIT |
                                              MODE5_LOWER_BIT |
                                              MODE_CLEAR);

    xEventGroupClearBits(e_ctrl_protocol, FAULT_BIT); // 清除故障位
    xEventGroupClearBits(e_ctrl_protocol, RUN_BIT);   // 清除运行位
}

// 设置故障状态，置FAULT_BIT位
int set_fault(void)
{
    xEventGroupSetBits(e_ctrl_protocol, FAULT_BIT);
    return 0;
}

// 清除故障状态，清除FAULT_BIT位
int clear_fault(void)
{
    xEventGroupClearBits(e_ctrl_protocol, FAULT_BIT);
    return 0;
}

// 查询是否处于故障状态，返回1表示故障，0表示正常
int get_fault(void)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & FAULT_BIT)
    {
        return 1;
    }
    return 0;
}



// 设置运行状态，置RUN_BIT位；如果处于故障状态则打印错误并返回-1
int set_run(void)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & FAULT_BIT)
    {
        ESP_LOGE(TAG, "Can't run while fault");
        ESP_LOGE(TAG, "Please clear fault first");
        return -1;
    }
    xEventGroupSetBits(e_ctrl_protocol, RUN_BIT);
    return 0;
}

// 清除运行状态，清除RUN_BIT位
int clear_run(void)
{
    xEventGroupClearBits(e_ctrl_protocol, RUN_BIT);
    return 0;
}

// 查询是否处于运行状态，返回1表示运行，0表示未运行
int get_run(void)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & RUN_BIT)
    {
        return 1;
    }
    return 0;
}

/**
 * @brief 设置运行模式
 * @param mode 传入模式号（0~6）
 * @return 当前设置的模式，错误返回-1
 * @note 运行中或故障中不允许切换模式
 */
int set_mode(int mode)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & RUN_BIT) // 运行时禁止切换模式
    {
        ESP_LOGE(TAG, "Can't change mode while running");
        return -1;
    }
    if (e_bits & FAULT_BIT) // 故障时禁止切换模式
    {
        ESP_LOGE(TAG, "Can't change mode while fault");
        ESP_LOGE(TAG, "Please clear fault first");
        return -1;
    }

    if (mode < 0 || mode > 6) // 非法模式号检测
    {
        ESP_LOGE(TAG, "Invalid mode");
        return -1;
    }

    switch (mode)
    {
    case 0:
        // 先清除所有模式事件位，再置位对应模式
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE0_BIT);
        break;
    case 1:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE1_BIT);
        break;
    case 2:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE2_BIT);
        break;
    case 3:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE3_BIT);
        break;
    case 4:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE4_BIT);
        break;
    case 5:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE5_UPPER_BIT);
        break;
    case 6:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE5_LOWER_BIT);
        break;
    case 7:
        xEventGroupClearBits(e_ctrl_protocol, MODE0_BIT | MODE1_BIT | MODE2_BIT | MODE3_BIT | MODE4_BIT | MODE5_UPPER_BIT | MODE5_LOWER_BIT | MODE_CLEAR);
        xEventGroupSetBits(e_ctrl_protocol, MODE_CLEAR);
        break;
    default:
        ESP_LOGE(TAG, "Invalid mode");
        return -1;
    }
    return mode;
}

/**
 * @brief 获取当前模式
 * @return 模式编号0~7，异常返回-1
 */
int get_mode(void)
{
    EventBits_t e_bits = xEventGroupGetBits(e_ctrl_protocol);

    if (e_bits & MODE0_BIT)
        return 0;
    if (e_bits & MODE1_BIT)
        return 1;
    if (e_bits & MODE2_BIT)
        return 2;
    if (e_bits & MODE3_BIT)
        return 3;
    if (e_bits & MODE4_BIT)
        return 4;
    if (e_bits & MODE5_UPPER_BIT)
        return 5;
    if (e_bits & MODE5_LOWER_BIT)
        return 6;
    if (e_bits & MODE_CLEAR)
        return 7;

    return -1;
}

// 协议处理主函数，根据输入命令修改状态并返回结果字符串
void ctrl_protocol(const char *input, char *output, int max_len)
{
    ESP_LOGI(TAG, "Received len:%d command: %s", strnlen(input, max_len), input); // 打印输入命令

    // 各模式切换命令判断，前置判断运行/故障状态，不允许非法切换
    if (strncmp(input, "CMD:MODE0", strnlen("CMD:MODE0", max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(0);
        snprintf(output, max_len, "CMD:MODE1,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE1\r\n", strnlen("CMD:MODE1\r\n", max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(1);
        snprintf(output, max_len, "CMD:MODE1,OK\r\n");
        return;
    }
    // 以下命令类似，省略重复注释...
    else if (strncmp(input, "CMD:MODE2\r\n", strnlen("CMD:MODE2\r\n", max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(2);
        snprintf(output, max_len, "CMD:MODE2,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE3\r\n", strnlen("CMD:MODE3\r\n", max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(3);
        snprintf(output, max_len, "CMD:MODE3,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE4\r\n", strnlen("CMD:MODE4\r\n", max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }

        set_mode(4);
        snprintf(output, max_len, "CMD:MODE4,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE5_UPPER\r\n", strnlen("CMD:MODE5_UPPER\r\n", max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        set_mode(5);
        snprintf(output, max_len, "CMD:MODE5_UPPER,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE5_LOWER\r\n", strnlen("CMD:MODE5_LOWER\r\n", max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        set_mode(6);
        snprintf(output, max_len, "CMD:MODE5_LOWER,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE_CLEAR\r\n", strnlen("CMD:MODE_CLEAR\r\n", max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        set_mode(7);
        snprintf(output, max_len, "CMD:MODE_CLEAR,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE_START\r\n", strnlen("CMD:MODE_START\r\n", max_len)) == 0)
    {
        if (get_run() == 1)
        {
            ESP_LOGE(TAG, "Can't change run mode while running");
            ESP_LOGE(TAG, "Please stop running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't change run mode while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        set_run();
        snprintf(output, max_len, "CMD:MODE_START,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:MODE_STOP\r\n", strnlen("CMD:MODE_STOP\r\n", max_len)) == 0)
    {
        if (get_run() == 0)
        {
            ESP_LOGE(TAG, "Can't stop while not running");
            ESP_LOGE(TAG, "Please start running first");
            goto err_;
        }
        if (get_fault() == 1)
        {
            ESP_LOGE(TAG, "Can't stop while fault");
            ESP_LOGE(TAG, "Please clear fault first");
            goto err_;
        }
        clear_run();
        snprintf(output, max_len, "CMD:MODE_STOP,OK\r\n");
        return;
    }
    else if (strncmp(input, "CMD:GET_STATUS\r\n", strnlen("CMD:GET_STATUS\r\n", max_len)) == 0)
    {
        if (get_fault() == 1)
        {
            snprintf(output, max_len, "CMD:FAULT\r\n");
            return;
        }
        if (get_run() == 1)
        {
            snprintf(output, max_len, "CMD:RUN\r\n");
            return;
        }
        else
        {
            snprintf(output, max_len, "CMD:STOP\r\n");
            return;
        }
    }

err_:
    ESP_LOGE(TAG, "Invalid command");
    snprintf(output, max_len, "CMD:ERR\r\n");
    return;
}
