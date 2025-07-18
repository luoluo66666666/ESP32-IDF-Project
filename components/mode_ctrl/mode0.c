
// #include "esp_log.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "mode_ctrl.h"
// #include <stdio.h>

// static const char *TAG = "MODE0";

// extern int do_pin[26];
// extern int di_pin[6];

// int mode0(void)
// {
//     int status = 0;   // Initialize status variable
//     int time_cnt = 0; // Initialize time counter

//     int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};

//     ESP_LOGI(TAG, "Entering mode 0");
//     for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); i++)
//     {
//         set_do_pin(i, 0);
//     }
//     for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); i++)
//     {
//         di_level[i] = get_di_pin(i);
//         // ESP_LOGI(TAG, "DI%d pin: %d level: %d", i, di_pin[i], di_level[i]);
//     }
//     while (true)
//     {
//         // ESP_LOGI(TAG, "Running mode 0");
//         ESP_LOGI(TAG, "mode0 status:%d time_cnt:%d", status, time_cnt);
//         // await_pause_and_restore()  //等待暂停按钮被按下
//         if (status == 0) // cool water 1
//         {
//             if (time_cnt == 0)
//             {
//                 set_do_pin(7, 1); // Turn on the water pump
//                 for (size_t i = 12; i < 22; i++)
//                 {
//                     set_do_pin(i, 1);
//                 }
//                 set_do_pin(8, 1); // Turn on the first water valve
//             }

//             time_cnt++;
//             if (time_cnt >= 5)
//             {
//                 status = 1;
//                 time_cnt = 0;
//             }
//         }
//         else if (status == 1) // flush water 2
//         {
//             if (time_cnt == 0)
//             {
//                 set_do_pin(3, 1); // Turn on the heat active
//                 set_do_pin(0, 1); // Turn on the updown push rod
//                 set_do_pin(1, 0); // Turn on the leftright push rod
//             }

//             set_do_pin(0, !get_do_pin(0)); // Toggle the updown push rod
//             set_do_pin(1, !get_do_pin(1)); // Toggle the leftright push rod
//             time_cnt++;
//             if (time_cnt >= 30)
//             {
//                 status = 2;
//                 time_cnt = 0;
//             }
//         }
//         else if (status == 2) // pause 3
//         {
//             if (time_cnt == 0)
//             {
//                 set_do_pin(7, 0); // Turn off the water pump
//                 set_do_pin(3, 0); // Turn off the heat active
//                 set_do_pin(0, 0); // Turn off the updown push rod
//                 set_do_pin(1, 0); // Turn off the leftright push rod
//                 for (size_t i = 12; i < 22; i++)
//                 {
//                     set_do_pin(i, 0); // Turn off all water valves
//                 }
//             }
//             time_cnt++;
//             if (time_cnt >= 10)
//             {
//                 status = 3; // Move to the next status
//                 time_cnt = 0;
//             }
//         }

//         vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1000 milliseconds (1 second)
//     }

//     return 0; // Return 0 to indicate success
// }

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_ctrl.h"
#include <stdio.h>

static const char *TAG = "MODE0";

extern int do_pin[26];
extern int di_pin[6];

/*-----------------------------------------------------------
 *  帮助宏
 *----------------------------------------------------------*/
#define TURN_ON(pin) set_do_pin((pin), 1)
#define TURN_OFF(pin) set_do_pin((pin), 0)
#define TOGGLE(pin) set_do_pin((pin), !get_do_pin((pin)))

#define GROUP_ON(start, end)                         \
    do                                               \
    {                                                \
        for (size_t _i = (start); _i <= (end); ++_i) \
        {                                            \
            TURN_ON(_i);                             \
        }                                            \
    } while (0)

#define GROUP_OFF(start, end)                        \
    do                                               \
    {                                                \
        for (size_t _i = (start); _i <= (end); ++_i) \
        {                                            \
            TURN_OFF(_i);                            \
        }                                            \
    } while (0)

static inline void delay_1s(void)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}

/*-----------------------------------------------------------
 *  mode0 主体
 *----------------------------------------------------------*/
int mode0(void)
{
    int status = 0;
    int time_cnt = 0;
    int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};

    ESP_LOGI(TAG, "Entering mode 0");

    /* 初始化所有 DO */
    for (size_t i = 0; i < sizeof(do_pin) / sizeof(do_pin[0]); ++i)
    {
        TURN_OFF(i);
    }

    /* 读取 DI，必要时可作暂停/急停等判断 */
    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); ++i)
    {
        di_level[i] = get_di_pin(i);
    }

    /*------------------ 主循环 ------------------*/
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

        /*------------------ 状态机 ------------------*/
        if (status == 0)
        { /* 冷水 1（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7);       /* 水泵 */
                GROUP_ON(12, 21); /* 12‑21 号电磁阀 */
                TURN_ON(8);       /* 第一路水阀 */
            }
            if (++time_cnt >= 5)
            {
                status = 1;
                time_cnt = 0;
            }
        }
        else if (status == 1)
        { /* 冲水 2（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(3);  /* 加热 */
                TURN_ON(0);  /* 上下推杆 */
                TURN_OFF(1); /* 左右推杆 */
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 30)
            {
                status = 2;
                time_cnt = 0;
            }
        }
        else if (status == 2)
        { /* 暂停 3（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(7);
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
        }
        else if (status == 3)
        { /* 洗发水 4（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 5)
            {
                status = 4;
                time_cnt = 0;
            }
        }
        else if (status == 4)
        { /* 暂停 5（10 s）*/
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
        }
        else if (status == 5)
        { /* 冲水 6（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 30)
            {
                status = 6;
                time_cnt = 0;
            }
        }
        else if (status == 6)
        { /* 暂停 7（10 s）*/
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
        }
        else if (status == 7)
        { /* 洗发水 8（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 5)
            {
                status = 8;
                time_cnt = 0;
            }
        }
        else if (status == 8)
        { /* 暂停 9（10 s）*/
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
                status = 9;
                time_cnt = 0;
            }
        }
        else if (status == 9)
        { /* 冲水 10（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 30)
            {
                status = 10;
                time_cnt = 0;
            }
        }
        else if (status == 10)
        { /* 暂停 12（10 s）*/
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
        }
        else if (status == 11)
        { /* 护发素 12（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(6);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 5)
            {
                status = 12;
                time_cnt = 0;
            }
        }
        else if (status == 12)
        { /* 暂停 13（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(7);
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
        }
        else if (status == 13)
        { /* 冲水 14（30 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 30)
            {
                status = 14;
                time_cnt = 0;
            }
        }
        else if (status == 14)
        { /* 暂停 15（10 s）*/
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
        }
        else if (status == 15)
        { /* 洗发水 16（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 5)
            {
                status = 16;
                time_cnt = 0;
            }
        }
        else if (status == 16)
        { /* 暂停 17（10 s）*/
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
                status = 17;
                time_cnt = 0;
            }
        }
        else if (status == 17)
        { /* 冲水 18（40 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 18;
                time_cnt = 0;
            }
        }
        else if (status == 18)
        { /* 暂停 19（10 s）*/
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
        }
        else if (status == 19)
        { /* 护发素 20（5 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(6);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 5)
            {
                status = 20;
                time_cnt = 0;
            }
        }
        else if (status == 20)
        { /* 暂停 21（10 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(11); /* 额外开启 pin11 */
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
        }
        else if (status == 21)
        { /* 冲水 22（60 s）*/
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                GROUP_ON(12, 21);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 60)
            {
                status = 22;
                time_cnt = 0;
            }
        }
        else if (status == 22)
        { /* 暂停 23（10 s）*/
            if (time_cnt == 0)
            {
                TURN_OFF(7);
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
        }
        else if (status == 23)
        { /* 结束 / 复位 */
            TURN_OFF(10);
            TURN_OFF(11);
            status = 0;
            time_cnt = 0;
            ESP_LOGI(TAG, "mode0 finished.");
            return 0;
        }

        /*--------------------------------------------------*/
        delay_1s(); /* 每个循环 1 s */
    }

    /* 不会到达这里 */
    return 0;
}
