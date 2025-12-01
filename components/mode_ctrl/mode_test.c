#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_ctrl.h"
#include "ctrl_protocol.h"
#include <stdio.h>

static const char *TAG = "MODE_TEST";

#define Motor_RUN_BIT BIT7
#define Motor_STOP_BIT BIT8
#define Motor_GET_BIT BIT9

extern int do_pin[26];
extern int di_pin[6];
extern uint8_t last_di;
extern uint8_t cur_di;

#define POLE_EVENT_BIT (1 << 0)
extern EventGroupHandle_t event_motor_ctrl;

/*******************************************************************************
****函数功能: MODE0
****作者名称: Luo
****创建日期: 2025-08-08 14:09:46
********************************************************************************/
void test_task(void *pvParameters)
{
    int status = 0;
    int time_cnt = 0;
    int runtime = 0; /* 统计执行时长（秒） */
    // int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};

    ESP_LOGI(TAG, "Entering mode 0");

    /* 初始化所有 DO */
    for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); ++i)
    {
        TURN_OFF(i);
    }

    /* 读取 DI，必要时可作暂停/急停等判断 */
    // for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); ++i)
    // {
    //     di_level[i] = get_di_pin(i);
    // }

    /*------------------ 主循环 ------------------*/
    while (true)
    {

        ESP_LOGI(TAG, "status:%d time_cnt:%d", status, time_cnt);

        /* 暂停键，放在这里检测 (await_ pause_and_restore) */
        if (get_di_pin(2) == 1) // 假设 DI1 是暂停键
        {
            vTaskDelay(pdMS_TO_TICKS(50)); // 简单防抖50ms
            ESP_LOGI(TAG, "Paused");
            for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); ++i)
            {
                do_level[i] = get_do_pin(i); // 记录当前 DO 状态
                TURN_OFF(i);                 // 关掉所有 DO
            }

            while (get_di_pin(2) == 1) // 等待暂停键释放
            {
                vTaskDelay(pdMS_TO_TICKS(100)); // 每 100 毫秒检查一次
            }
            for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); ++i)
            {
                set_do_pin(i, do_level[i]); // 恢复 DO 状态
            }
            ESP_LOGI(TAG, "Resumed");
        }

        /*------------------ 状态机 ------------------*/
        switch (status)
        {
        case 0: /* 冷水 1（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7); /* 清水阀 */
                TURN_ON(16);
                TURN_ON(17); /* 12‑19 号电磁阀 */
                TURN_ON(8);  /* 第一路水阀 */
            }
            if (++time_cnt >= 5)
            {
                status = 1;
                time_cnt = 0;
            }
            break;

        case 1: /* 冲水 2（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(3); /* 加热 */
                // 触发 Pole_motor_control_task 执行
                motor_run();
            }

            if (++time_cnt >= 30)
            {
                // 停止 Pole_motor_control_task
                motor_stop();
                status = 2;
                time_cnt = 0;
            }
            break;

        case 2: /* 暂停 3（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 3;
                time_cnt = 0;
            }
            break;

        case 3: /* 洗发水 4（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);
                TURN_ON(5);
                // 触发 Pole_motor_control_task 执行
                motor_run();
            }

            if (++time_cnt >= 5)
            {
                motor_stop();
                status = 4;
                time_cnt = 0;
            }
            break;

        case 4: /* 暂停 5（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
                TURN_OFF(5);
            }
            if (++time_cnt >= 10)
            {
                status = 5;
                time_cnt = 0;
            }
            break;

        case 5: /* 冲水 6（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);
                motor_run();
            }
            if (++time_cnt >= 30)
            {
                motor_stop();
                status = 6;
                time_cnt = 0;
            }
            break;

        case 6: /* 暂停 7（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 7;
                time_cnt = 0;
            }
            break;

        case 7: /* 洗发水 8（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);
                TURN_ON(5);
                motor_run();
            }
            if (++time_cnt >= 5)
            {
                motor_stop();
                status = 8;
                time_cnt = 0;
            }
            break;

        case 8: /* 暂停 9（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
                TURN_OFF(5);
            }
            if (++time_cnt >= 10)
            {
                status = 9;
                time_cnt = 0;
            }
            break;

        case 9: /* 冲水 10（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);
                motor_run();
            }
            if (++time_cnt >= 30)
            {
                motor_stop();
                status = 10;
                time_cnt = 0;
            }
            break;

        case 10: /* 暂停 11（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 11;
                time_cnt = 0;
            }
            break;

        case 11: /* 护发素 12（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);
                TURN_ON(6);
                motor_run();
            }
            if (++time_cnt >= 5)
            {
                motor_stop();
                status = 12;
                time_cnt = 0;
            }
            break;

        case 12: /* 暂停 13（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
                TURN_OFF(6);
            }
            if (++time_cnt >= 10)
            {
                status = 13;
                time_cnt = 0;
            }
            break;

        case 13: /*14 结束 / 复位 */
            ESP_LOGI(TAG, "mode 0 finished");
            for (size_t i = 0; i < 26; ++i)
            {
                TURN_OFF(i);
            }
            ESP_LOGI(TAG, "Mode0 finished, deleting task");
            // 设置电机任务的 FINISH_BIT，电机任务收到后自动收杆
            xEventGroupSetBits(event_motor_ctrl, Motor_Finsh_BIT);

            vTaskDelete(NULL); // 删除自己
            break;

        default:
            ESP_LOGE(TAG, "Unknown status %d", status);
            for (size_t i = 0; i < 26; ++i)
            {
                TURN_OFF(i);
            }
            vTaskDelete(NULL); // 退出任务
            break;
        }

        /*------------------------时间基准--------------------------*/
        runtime++;
        delay_1s(); /* 延时 1 秒 */
    }
}

void Water_level_task(void *pvParameters)
{
    int water_flag = 0;
    while (1)
    {
        water_flag = get_di_pin(4);
        if (water_flag == 1)
        {
        }
    }
}

/*********** 6分钟测试 ************** */
void sixmin_test_task(void *pvParameters)
{
    int status = 0;
    int time_cnt = 0;
    int runtime = 0; /* 统计执行时长（秒） */
    int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};
    cur_di = input_state_change_handler();

    if (check_cross_loop_lock())
    {
        ESP_LOGI(TAG, "Water noenough, abort mode test");
        vTaskDelete(NULL); // 删除当前任务
    }

    ESP_LOGI(TAG, "Entering mode 0");
    /* 初始化所有 DO */
    for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); ++i)
    {
        TURN_OFF(i);
    }

    /*------------------ 主循环 ------------------*/
    while (1)
    {

        ESP_LOGI(TAG, "status:%d time_cnt:%d", status, time_cnt);
        cur_di = input_state_change_handler();

        /*------------------ 状态机 ------------------*/
        switch (status)
        {
        case 0: /* 冷水 1（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7); /* 清水阀 */
                TURN_ON(8); /* 放水阀 */
                TURN_ON(16);
                TURN_ON(17); /* 12‑19 号电磁出水阀 */
            }
            if (++time_cnt >= 5)
            {
                status = 1;
                time_cnt = 0;
            }
            break;

        case 1: /* 冲水 2（30 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(8); /* 关放水阀 */
                TURN_ON(3);  /* 开高压泵 */
                // 触发 Pole_motor_control_task 执行
                motor_run();
            }

            if (++time_cnt >= 30)
            {
                // 停止 Pole_motor_control_task
                motor_stop();
                status = 2;
                time_cnt = 0;
            }
            break;

        case 2: /* 暂停 3（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 3;
                time_cnt = 0;
            }
            break;

        case 3: /* 洗发水 4（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(5);
                TURN_ON(16);
                TURN_ON(17);
                // 触发 Pole_motor_control_task 执行
                motor_run();
            }

            if (++time_cnt >= 5)
            {
                motor_stop();
                status = 4;
                time_cnt = 0;
            }
            break;

        case 4: /* 暂停 5（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
                TURN_OFF(5);
            }
            if (++time_cnt >= 10)
            {
                status = 5;
                time_cnt = 0;
            }
            break;

        case 5: /* 冲水 6（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);
                motor_run();
            }
            if (++time_cnt >= 30)
            {
                motor_stop();
                status = 6;
                time_cnt = 0;
            }
            break;

        case 6: /* 暂停 7（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 7;
                time_cnt = 0;
            }
            break;

        case 7: /* 洗发水 8（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(5);
                TURN_ON(16);
                TURN_ON(17);

                motor_run();
            }
            if (++time_cnt >= 5)
            {
                motor_stop();
                status = 8;
                time_cnt = 0;
            }
            break;

        case 8: /* 暂停 9（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(5);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 9;
                time_cnt = 0;
            }
            break;

        case 9: /* 冲水 10（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);
                motor_run();
            }
            if (++time_cnt >= 30)
            {
                motor_stop();
                status = 10;
                time_cnt = 0;
            }
            break;

        case 10: /* 暂停 11（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 11;
                time_cnt = 0;
            }
            break;

        case 11: /* 护发素 12（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(6);
                TURN_ON(16);
                TURN_ON(17);

                motor_run();
            }
            if (++time_cnt >= 5)
            {
                motor_stop();
                status = 12;
                time_cnt = 0;
            }
            break;

        case 12: /* 暂停 13（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(6);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 13;
                time_cnt = 0;
            }
            break;

        case 13: /* 冲水 14（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);
                motor_run();
            }
            if (++time_cnt >= 30)
            {
                motor_stop();
                status = 14;
                time_cnt = 0;
            }
            break;

        case 14: /* 暂停 15（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 15;
                time_cnt = 0;
            }
            break;

        case 15: /* 洗发水 16（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(5);
                TURN_ON(16);
                TURN_ON(17);

                motor_run();
            }
            if (++time_cnt >= 5)
            {
                motor_stop();
                status = 16;
                time_cnt = 0;
            }
            break;

        case 16: /* 暂停 17（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(5);
                TURN_OFF(16);
                TURN_OFF(17);
            }
            if (++time_cnt >= 10)
            {
                status = 17;
                time_cnt = 0;
            }
            break;

        case 17: /* 冲水 18（40 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);

                motor_run();
            }

            if (++time_cnt >= 40)
            {
                // 停止 Pole_motor_control_task
                motor_stop();
                status = 18;
                time_cnt = 0;
            }
            break;

        case 18: /* 暂停 19（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);
        
            }
            if (++time_cnt >= 10)
            {
                status = 19;
                time_cnt = 0;
            }
            break;

        case 19: /* 中药 20（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(22);
                TURN_ON(16);
                TURN_ON(17);

                motor_run();
            }
            if (++time_cnt >= 5)
            {
                motor_stop();
                status = 20;
                time_cnt = 0;
            }
            break;

        case 20: /* 暂停 21（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(22);
                TURN_OFF(16);
                TURN_OFF(17);

                TURN_ON(11); // 打开直排
            }
            if (++time_cnt >= 10)
            {
                status = 21;
                time_cnt = 0;
            }
            break;

        case 21: /* 冲水 22（60 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(17);
                motor_run();
            }
            if (++time_cnt >= 30)
            {
                motor_stop();
                status = 22;
                time_cnt = 0;
            }
            break;

        case 22: /* 暂停 23（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(17);

                TURN_OFF(11); // 关闭直排
                TURN_OFF(25);
            }
            if (++time_cnt >= 10)
            {
                status = 23;
                time_cnt = 0;
            }
            break;

        case 23: /*24 结束 / 复位 */
            ESP_LOGI(TAG, "mode 0 finished");
            for (size_t i = 0; i < 26; ++i)
            {
                TURN_OFF(i);
            }
            ESP_LOGI(TAG, "Mode0 finished, deleting task");
            // 设置电机任务的 FINISH_BIT，电机任务收到后自动收杆
            xEventGroupSetBits(event_motor_ctrl, Motor_Finsh_BIT);

            vTaskDelete(NULL); // 删除自己
            break;

        default:
            ESP_LOGE(TAG, "Unknown status %d", status);
            for (size_t i = 0; i < 26; ++i)
            {
                TURN_OFF(i);
            }
            vTaskDelete(NULL); // 退出任务
            break;
        }

        /*------------------------时间基准--------------------------*/
        runtime++;
        delay_1s(); /* 延时 1 秒 */
    }
}

void start_mode_test(void)
{
    // xTaskCreate(test_task, "test_task", 4096, NULL, 5, NULL);
    xTaskCreate(sixmin_test_task, "sixmin_test_task", 4096, NULL, 5, NULL);
}
