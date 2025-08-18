#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_ctrl.h"

static const char *TAG = "MODE1";

extern int do_pin[26];
extern int di_pin[6];

/*******************************************************************************
****@brief: MODE1
****@author: Luo
****@date: 2025-08-08 15:48:26
********************************************************************************/
int mode1(void)
{
    int status = 0;
    int time_cnt = 0;
    int runtime = 0; /* 统计执行时长（秒） */
    int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};

    ESP_LOGI(TAG, "Entering mode 1");

    /* 进入前确保全部 DO 关闭 */
    for (size_t i = 0; i < 26; ++i)
        TURN_OFF(i);

    /* 读取 DI，必要时可作暂停/急停等判断 */
    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); ++i)
    {
        di_level[i] = get_di_pin(i);
    }

    while (true)
    {

        ESP_LOGI(TAG, "status:%d time_cnt:%d", status, time_cnt);

        /* 暂停键，放在这里检测 (await_pause_and_restore) */
        if (get_di_pin(2) == 1) // 假设 DI1 是暂停键
        {
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

        switch (status)
        {
        case 0: // 放冷水  (5 s)
            if (time_cnt == 0)
            {
                TURN_ON(7);
                GROUP_ON(12, 21);
                TURN_ON(8);
            }
            if (++time_cnt >= 5)
            {
                TURN_OFF(8);
                status = 1;
                time_cnt = 0;
            }
            break;

        case 1: // 冲水 (40 s)
            if (time_cnt == 0)
            {
                TURN_ON(3);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 2;
                time_cnt = 0;
            }
            break;

        case 2: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
                GROUP_OFF(12, 21);
            }
            if (++time_cnt >= 10)
            {
                status = 3;
                time_cnt = 0;
            }
            break;

        case 3: // 洗发水 (10 s)
            if (time_cnt == 0)
            {
                TURN_ON(3);
                TURN_ON(7);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 4;
                time_cnt = 0;
            }
            break;

        case 4: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(5);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 5;
                time_cnt = 0;
            }
            break;

        case 5: // 冲水 (40 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 6;
                time_cnt = 0;
            }
            break;

        case 6: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
                GROUP_OFF(12, 21);
            }
            if (++time_cnt >= 10)
            {
                status = 7;
                time_cnt = 0;
            }
            break;

        case 7: // 洗发水 (10 s)
            if (time_cnt == 0)
            {
                TURN_ON(3);
                TURN_ON(9);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 8;
                time_cnt = 0;
            }
            break;

        case 8: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(5);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 9;
                time_cnt = 0;
            }
            break;

        case 9: // 冲水 (40 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 10;
                time_cnt = 0;
            }
            break;

        case 10: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
                GROUP_OFF(12, 21);
            }
            if (++time_cnt >= 10)
            {
                status = 11;
                time_cnt = 0;
            }
            break;

        case 11: // 护发素 (10 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(6);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 12;
                time_cnt = 0;
            }
            break;

        case 12: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(6);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 13;
                time_cnt = 0;
            }
            break;

        case 13: // 冲水 (40 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 14;
                time_cnt = 0;
            }
            break;

        case 14: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
                GROUP_OFF(12, 21);
            }
            if (++time_cnt >= 10)
            {
                status = 15;
                time_cnt = 0;
            }
            break;

        case 15: // 洗发水 (10 s)
            if (time_cnt == 0)
            {
                TURN_ON(3);
                TURN_ON(9);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 16;
                time_cnt = 0;
            }
            break;

        case 16: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(5);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 17;
                time_cnt = 0;
            }
            break;

        case 17: // 冲水 (40 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 18;
                time_cnt = 0;
            }
            break;

        case 18: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
                GROUP_OFF(12, 21);
            }
            if (++time_cnt >= 10)
            {
                status = 19;
                time_cnt = 0;
            }
            break;

        case 19: // 护发素 (10 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(6);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 20;
                time_cnt = 0;
            }
            break;

        case 20: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(6);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 21;
                time_cnt = 0;
            }
            break;

        case 21: // 冲水 (40 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 22;
                time_cnt = 0;
            }
            break;

        case 22: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
                GROUP_OFF(12, 21);
            }
            if (++time_cnt >= 10)
            {
                status = 23;
                time_cnt = 0;
            }
            break;

        case 23: // 洗发水 (10 s)
            if (time_cnt == 0)
            {
                TURN_ON(3);
                TURN_ON(9);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 24;
                time_cnt = 0;
            }
            break;

        case 24: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(5);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 25;
                time_cnt = 0;
            }
            break;

        case 25: // 冲水 (40 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 26;
                time_cnt = 0;
            }
            break;

        case 26: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
                GROUP_OFF(12, 21);
            }
            if (++time_cnt >= 10)
            {
                status = 27;
                time_cnt = 0;
            }
            break;

        case 27: // 护发素 (10 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(6);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 28;
                time_cnt = 0;
            }
            break;

        case 28: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(6);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 29;
                time_cnt = 0;
            }
            break;

        case 29: // 冲水 (40 s)
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 30;
                time_cnt = 0;
            }
            break;

        case 30: // 暂停 (10 s)
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
                GROUP_OFF(12, 21);
            }
            if (++time_cnt >= 10)
            {
                status = 31;
                time_cnt = 0;
            }
            break;

        case 31: // 冲水 (10 s)
            if (time_cnt == 0)
            {
                TURN_ON(7);
                GROUP_ON(12, 21);
                TURN_ON(8);
            }
            if (++time_cnt >= 10)
            {
                TURN_OFF(8);
                status = 32;
                time_cnt = 0;
            }
            break;

        case 32: // 结束
            ESP_LOGI(TAG, "mode 1 finished");
            for (size_t i = 0; i < 26; ++i)
                TURN_OFF(i);
            return 0;

        default:
            ESP_LOGE(TAG, "Unknown status %d", status);
            return -1;
        }

        runtime++;
        delay_1s(); /* 延时 1 秒 */
    }
}