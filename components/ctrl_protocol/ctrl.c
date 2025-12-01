#include "ctrl_protocol.h"
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "mode_ctrl.h" // 引入模式控制相关函数的头文件
#include "rs485_water_valve.h"
extern EventGroupHandle_t event_ctrl_protocol; // 事件组句柄，用于管理运行/故障/模式状态
// EventGroupHandle_t event_ctrl_protocol; // 事件组句柄，用于管理运行/故障/模式状态
const char *TAG = "CTRL_PROTOCOL"; // 日志TAG

/*******************************************************************************
****@brief: 左右撑杆电机控制
*每个撑杆电机接入正负极,每个极性控制一个方向，两个io不能同时开启，切换时需关闭io输出
*左右DO0 DO1 上下DO20 21
****@author: Luo
****@date: 2025-08-19 15:21:44
********************************************************************************/
#define STEP_DELAY_MS 50
#define PHASE_TIME_MS 5000

typedef enum
{
    POLE_RETRACTED = 0,
    POLE_EXTENDED = 1
} pole_state_t;

EventGroupHandle_t event_motor_ctrl;

void Pole_motor_control_task(void *p)
{
    pole_state_t pole1_state = POLE_RETRACTED;
    pole_state_t pole2_state = POLE_RETRACTED;

    const TickType_t step_delay = pdMS_TO_TICKS(STEP_DELAY_MS);
    const TickType_t phase_time = pdMS_TO_TICKS(PHASE_TIME_MS);
    TURN_OFF(0);
    TURN_OFF(1);
    TURN_OFF(20);
    TURN_OFF(21);

    while (1)
    {
        // 等待 RUN_BIT 或 FINISH_BIT
        xEventGroupWaitBits(event_motor_ctrl, Motor_RUN_BIT | Motor_Finsh_BIT,
                            pdFALSE, pdFALSE, portMAX_DELAY);

        // === FINISH 优先级最高 ===
        if (xEventGroupGetBits(event_motor_ctrl) & Motor_Finsh_BIT)
        {
        finsh_motor:
            // 收杆
            TURN_OFF(0);
            TURN_ON(1);
            pole1_state = POLE_RETRACTED;
            TURN_OFF(20);
            TURN_ON(21);
            pole2_state = POLE_RETRACTED;
            vTaskDelay(pdMS_TO_TICKS(5000)); // 给收杆动作预留时间
            TURN_OFF(21);
            TURN_OFF(1);

            xEventGroupClearBits(event_motor_ctrl, Motor_Finsh_BIT | Motor_RUN_BIT | Motor_STOP_BIT);
            ESP_LOGI(TAG, "Motor FINISH detected, all retracted");
            continue; // 等待下一次 RUN/FINISH
        }

        // === RUN 状态机 ===
        while (xEventGroupGetBits(event_motor_ctrl) & Motor_RUN_BIT)
        {
            if (pole1_state == POLE_RETRACTED && pole2_state == POLE_RETRACTED)
            {
                TURN_OFF(0);
                TURN_ON(1);
                pole1_state = POLE_EXTENDED;
                TURN_OFF(21);
                TURN_ON(20);
                pole2_state = POLE_RETRACTED;
            }
            else if (pole1_state == POLE_EXTENDED && pole2_state == POLE_RETRACTED)
            {
                TURN_OFF(1);
                TURN_ON(0);
                pole1_state = POLE_RETRACTED;
                TURN_OFF(20);
                TURN_ON(21);
                pole2_state = POLE_EXTENDED;
            }
            else if (pole1_state == POLE_RETRACTED && pole2_state == POLE_EXTENDED)
            {
                TURN_OFF(0);
                TURN_ON(1);
                pole1_state = POLE_EXTENDED;
                TURN_OFF(21);
                TURN_ON(20);
                pole2_state = POLE_RETRACTED;
            }
            else
            {
                TURN_OFF(0);
                TURN_OFF(1);
                TURN_OFF(20);
                TURN_OFF(21);
            }

            // 阶段延时，随时响应 STOP/FINISH
            TickType_t start_tick = xTaskGetTickCount();
            while (xTaskGetTickCount() - start_tick < phase_time)
            {
                EventBits_t bits = xEventGroupGetBits(event_motor_ctrl);
                if (bits & Motor_STOP_BIT)
                    goto stop_motor;
                if (bits & Motor_Finsh_BIT)
                    goto finsh_motor;

                vTaskDelay(step_delay);
            }
        }

    stop_motor:
        // 紧急停止，只断电，不改变 state
        TURN_OFF(0);
        TURN_OFF(1);
        TURN_OFF(20);
        TURN_OFF(21);
        xEventGroupClearBits(event_motor_ctrl, Motor_STOP_BIT | Motor_RUN_BIT);
        ESP_LOGI(TAG, "Motor STOP detected, hold state P1=%d, P2=%d",
                 pole1_state, pole2_state);
        continue; // 回到等待 RUN 或 FINISH
    }
}

/*******************************************************************************
****函数功能: 初始化控制协议
****作者名称: Luo
****创建日期: 2025-07-22 08:53:35
****最后编辑时间:
****最后编辑者: Luo
********************************************************************************/
void ctrl_protocol_init(void)
{
    event_ctrl_protocol = xEventGroupCreate(); // 创建事件组
    event_motor_ctrl = xEventGroupCreate();
    if (event_ctrl_protocol == NULL)
    {
        ESP_LOGE(TAG, "Failed to create event group");
    }

    // 事件组创建后，初始化所有标志位为0
    xEventGroupClearBits(event_ctrl_protocol, Mode0_BIT | Mode1_BIT | Mode2_BIT |
                                                  Mode3_BIT | Mode4_BIT | Mode5_UPPER_BIT | Mode5_LOWER_BIT |
                                                  RUN_BIT | FAULT_BIT);
    xEventGroupClearBits(event_motor_ctrl, Motor_RUN_BIT | Motor_STOP_BIT | Motor_GET_BIT);
}

/*******************************************************************************
****@brief:撑杆的运行和停止
****@author: Luo
****@date: 2025-08-21 16:37:49
********************************************************************************/
int motor_run(void)
{
    xEventGroupSetBits(event_motor_ctrl, Motor_RUN_BIT);
    return 0;
}

int motor_stop(void)
{
    xEventGroupSetBits(event_motor_ctrl, Motor_STOP_BIT);
    return 0;
}

/*******************************************************************************
****函数功能: 获取故障状态
****作者名称: Luo
****创建日期: 2025-07-22 10:39:45
********************************************************************************/
bool get_fault_status(void)
{
    if (event_ctrl_protocol == NULL)
        return false;

    EventBits_t event_bits = xEventGroupGetBits(event_ctrl_protocol);
    return (event_bits & FAULT_BIT) != 0; // 返回故障状态
}

/*******************************************************************************
****函数功能: 获取运行状态
****作者名称: Luo
****创建日期: 2025-07-22 10:40:22
********************************************************************************/
bool get_run_status(void)
{
    if (event_ctrl_protocol == NULL)
        return false;

    EventBits_t event_bits = xEventGroupGetBits(event_ctrl_protocol);
    return (event_bits & RUN_BIT) != 0; // 返回运行状态
}

/*******************************************************************************
****函数功能: 获取当前模式状态
****作者名称: Luo
****创建日期: 2025-07-22 11:14:34
********************************************************************************/
int get_mode_status(void)
{
    if (event_ctrl_protocol == NULL)
        return -1;

    EventBits_t event_bits = xEventGroupGetBits(event_ctrl_protocol);
    if (event_bits & Mode0_BIT)
        return 0;
    if (event_bits & Mode1_BIT)
        return 1;
    if (event_bits & Mode2_BIT)
        return 2;
    if (event_bits & Mode3_BIT)
        return 3;
    if (event_bits & Mode4_BIT)
        return 4;
    if (event_bits & Mode5_UPPER_BIT)
        return 5;
    if (event_bits & Mode5_LOWER_BIT)
        return 6;

    return -1; // 如果没有匹配到任何模式，返回-1表示异常
}

/*******************************************************************************
****函数功能: 检测系统状态
****作者名称: Luo
****创建日期: 2025-07-22 11:15:03
********************************************************************************/
int check_status(void)
{
    if (get_fault_status())
    {
        ESP_LOGE(TAG, "System is in fault state");
        return -1;
    }

    int mode = get_mode_status();
    if (mode >= 0)
    {
        ESP_LOGI(TAG, "System is already running in mode: %d", mode);
        return -1;
    }

    if (get_run_status())
    {
        ESP_LOGI(TAG, "System is running, but no mode set.");
    }
    else
    {
        ESP_LOGI(TAG, "System is not running.");
    }

    return 0; // 可以切换
}

/*******************************************************************************
****函数功能: 设置模式，传入参数mode是bit位标志
****入口参数: mode:
****作者名称: Luo
****创建日期: 2025-07-22 11:20:32
********************************************************************************/
int set_mode(int mode)
{
    // EventBits_t event_bits = xEventGroupGetBits(event_ctrl_protocol);
    if (event_ctrl_protocol == NULL)
    {
        ESP_LOGE(TAG, "Event group not initialized");
        return -1;
    }
    // if (event_bits == 0 || event_bits == 1) // 运行时禁止切换模式
    // {
    //     ESP_LOGE(TAG, "Can't change mode while running");
    //     return -1;
    // }
    if (get_run_status() || get_fault_status())
    {
        ESP_LOGE(TAG, "Cannot change mode while running or in fault state");
        return -1;
    }
    int status = check_status();
    if (status < 0)
    {
        ESP_LOGE(TAG, "System is in fault state or already running in a mode");
        return -1; // 返回-1表示无法切换模式
    }

    // 清除所有模式位
    xEventGroupClearBits(event_ctrl_protocol, Mode0_BIT | Mode1_BIT | Mode2_BIT |
                                                  Mode3_BIT | Mode4_BIT | Mode5_UPPER_BIT | Mode5_LOWER_BIT);

    // 设置对应的模式位
    switch (mode)
    {
    case 0:
        xEventGroupSetBits(event_ctrl_protocol, Mode0_BIT);
        break;
    case 1:
        xEventGroupSetBits(event_ctrl_protocol, Mode1_BIT);
        break;
    case 2:
        xEventGroupSetBits(event_ctrl_protocol, Mode2_BIT);
        break;
    case 3:
        xEventGroupSetBits(event_ctrl_protocol, Mode3_BIT);
        break;
    case 4:
        xEventGroupSetBits(event_ctrl_protocol, Mode4_BIT);
        break;
    case 5:
        xEventGroupSetBits(event_ctrl_protocol, Mode5_UPPER_BIT);
        break;
    case 6:
        xEventGroupSetBits(event_ctrl_protocol, Mode5_LOWER_BIT);
        break;
    default:
        ESP_LOGE(TAG, "Invalid mode: %d", mode);
        return -1; // 返回-1表示无效模式
    }

    return mode; // 返回设置的模式
}

// 定义一个结构体，用于映射命令字符串与对应的模式索引
typedef struct ctrl_protocol_t
{
    char *cmd;      // 命令字符串
    int mode_index; // 模式索引
} mode_cmd;

// 定义所有支持的模式命令及其对应索引
mode_cmd mode_command[] =
    {
        {"CMD:MODE0", 0},
        {"CMD:MODE1", 1},
        {"CMD:MODE2", 2},
        {"CMD:MODE3", 3},
        {"CMD:MODE4", 4},
        {"CMD:MODE5_UPPER", 5},
        {"CMD:MODE5_LOWER", 6},
};

// 计算命令数组中元素数量，用于遍历
#define MODE_CMD_COUNT (sizeof(mode_command) / sizeof(mode_command[0]))

/*******************************************************************************
****函数功能: 处理控制协议命令
****入口参数:input: 输入命令字符串
****入口参数:output: 输出结果字符串
****入口参数:maxlen: 输出字符串的最大长度
****作者名称: Luo
****创建日期: 2025-07-22 14:23:06
********************************************************************************/
extern int do_pin[26];
void ctrl_protocol(char *input, char *output, int maxlen)
{
    ESP_LOGI(TAG, "Received command: %s", input);

    /* 1. 模式查询命令 */
    if (strncmp(input, "CMD:MODE_GET", strnlen("CMD:MODE_GET", maxlen)) == 0)
    {
        check_status();                                  // 检查当前模式状态
        snprintf(output, maxlen, "CMD:MODE_GET,OK\r\n"); // 回复模式查询成功
        return;
    }

    /* 2. 全部 DO 控制命令：ALL ON / ALL OFF */
    if (strcasecmp(input, "all on") == 0)
    {
        for (int i = 0; i < (sizeof(do_pin) / sizeof(do_pin[0])); i++)
        {
            set_do_pin(i, 1); // 打开所有 DO
        }
        snprintf(output, maxlen, "ALL,ON\r\n");
        return;
    }
    if (strcasecmp(input, "all off") == 0)
    {
        for (int i = 0; i < (sizeof(do_pin) / sizeof(do_pin[0])); i++)
        {
            set_do_pin(i, 0); // 关闭所有 DO
        }
        snprintf(output, maxlen, "ALL,OFF\r\n");
        return;
    }

    /* 3. 单路 DO 控制命令：格式 doX on/off/toggle */
    if (strncasecmp(input, "do", 2) == 0) // 检查前缀 "do"
    {
        int do_index = -1;    // DO 编号
        char action[8] = {0}; // 动作字符串（on/off/toggle）

        // 解析命令格式，例如 "do1 on" → do_index=1, action="on"
        if (sscanf(input, "do%d %7s", &do_index, action) == 2)
        {
            // 检查 DO 编号是否合法
            if (do_index >= 0 && do_index < (sizeof(do_pin) / sizeof(do_pin[0])))
            {
                if (strcasecmp(action, "on") == 0)
                {
                    set_do_pin(do_index, 1);
                    snprintf(output, maxlen, "do%d,ON\r\n", do_index);
                    return;
                }
                else if (strcasecmp(action, "off") == 0)
                {
                    set_do_pin(do_index, 0);
                    snprintf(output, maxlen, "do%d,OFF\r\n", do_index);
                    return;
                }
                else if (strcasecmp(action, "toggle") == 0)
                {
                    set_do_pin(do_index, !get_do_pin(do_index)); // 翻转当前状态
                    snprintf(output, maxlen, "do%d,TOGGLED\r\n", do_index);
                    return;
                }
            }
        }
    }

    /* 4. 模式切换命令 */
    for (int i = 0; i < MODE_CMD_COUNT; i++)
    {
        if (strncmp(input, mode_command[i].cmd, strnlen(mode_command[i].cmd, maxlen)) == 0)
        {
            int mode = set_mode(mode_command[i].mode_index);
            if (mode >= 0)
            {
                snprintf(output, maxlen, "%s,OK\r\n", mode_command[i].cmd);
            }
            else
            {
                snprintf(output, maxlen, "%s,ERROR\r\n", mode_command[i].cmd);
            }
            return;
        }
    }

    /* 5. 阀门控制命令： water set/get*/
    if (strncasecmp(input, "water", 5) == 0)
    {
        char action[16] = {0};
        int value = 0;
        RS485_init();

        // 支持格式：
        //   water set 45
        //   water get
        if (sscanf(input, "water %15s %d", action, &value) >= 1)
        {
            if (strcasecmp(action, "set") == 0)
            {
                // 写入阀门开度寄存器（40006 -> 0x0005）
                rs485_write_register(0x01, 0x0005, value);
                snprintf(output, maxlen, "WATER,SET,%d\r\n", value);
                return;
            }
            else if (strcasecmp(action, "get") == 0)
            {
                // 读取寄存器区间 40001~40013 (0x0000~0x000C)
                rs485_read_register(0x01, 0x0000, 12);
                snprintf(output, maxlen, "WATER,READ_OK\r\n");
                return;
            }
        }
    }

    /* 6. 恒温宝控制命令： temp set/get*/
    if (strncasecmp(input, "temp", 4) == 0)
    {
        uint8_t temp_addr = 1;
        char action[16] = {0};
        int value = 0;
        int args = sscanf(input, "temp %15s %d", action, &value);
        RS485_init();
        if (args >= 1)
        {
            if (strcasecmp(action, "set") == 0)
            {
                // 写入温度设定寄存器（0x0001），单位：℃
                temp_rs485_write_register(temp_addr, 0x0000, 0x00C0);
                vTaskDelay(pdMS_TO_TICKS(200));

                temp_rs485_write_register(temp_addr, 0x0001, (uint16_t)value);
                vTaskDelay(pdMS_TO_TICKS(200));

                temp_rs485_read_register(temp_addr, 0x0000, 2);
                vTaskDelay(pdMS_TO_TICKS(200));
                // 读取单个寄存器 0x0002（当前设定温度）
                temp_rs485_read_register(temp_addr, 0x0002, 1);
                vTaskDelay(pdMS_TO_TICKS(200));

                // 读取 0x0001 起始的 4 个寄存器（温度/流量等连续数据）
                temp_rs485_read_register(temp_addr, 0x0001, 4);
                vTaskDelay(pdMS_TO_TICKS(200));

                ESP_LOGI(TAG, "Test sequence finished");
                snprintf(output, maxlen, "TEMP,SET,%d\r\n", value);
            }
            else if (strcasecmp(action, "get") == 0)
            {
                // temp_rs485_write_register(temp_addr, 0x0000, 0x00C0);
                // vTaskDelay(pdMS_TO_TICKS(200));
                // 读取起始寄存器 0x0001，共4个寄存器（温度/设定温度/流量等）
                temp_rs485_read_register(temp_addr, 0x0000, 2);
                vTaskDelay(pdMS_TO_TICKS(200));
                temp_rs485_read_register(temp_addr, 0x0001, 4);
                snprintf(output, maxlen, "TEMP,READ_OK\r\n");
            }
            else if (strcasecmp(action, "test") == 0)
            {
                // 执行测试流程：开机 + 一次读取0x0001起始的4个寄存器
                temp_test_sequence();
                snprintf(output, maxlen, "TEMP,TEST,OK\r\n");
            }
            else
            {
                snprintf(output, maxlen, "TEMP,ERR,UNKNOWN_ACTION\r\n");
            }
        }
        else
        {
            snprintf(output, maxlen, "TEMP,ERR,BAD_FORMAT\r\n");
        }

        return;
    }
    /*  未知命令 */
    ESP_LOGE(TAG, "Invalid command");
    snprintf(output, maxlen, "CMD:ERR\r\n");
    return;
}
