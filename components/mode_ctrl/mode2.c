#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_ctrl.h"

static const char *TAG = "MODE2";

extern int do_pin[26];
extern int di_pin[6];

/*---------------- GPIO 快捷宏 ----------------*/
#define TURN_ON(pin) set_do_pin((pin), 1)
#define TURN_OFF(pin) set_do_pin((pin), 0)
#define TOGGLE(pin) set_do_pin((pin), !get_do_pin((pin)))

#define GROUP_ON(start, end)                         \
    do                                               \
    {                                                \
        for (size_t _i = (start); _i <= (end); ++_i) \
            TURN_ON(_i);                             \
    } while (0)
#define GROUP_OFF(start, end)                        \
    do                                               \
    {                                                \
        for (size_t _i = (start); _i <= (end); ++_i) \
            TURN_OFF(_i);                            \
    } while (0)

static inline void delay_1s(void) { vTaskDelay(pdMS_TO_TICKS(1000)); }

/*---------------- mode2 主体 ----------------*/
int mode2(void)
{
    int status = 0;
    int time_cnt = 0;
    int runtime = 0;
    int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};

    ESP_LOGI(TAG, "Entering mode 2");

    /* 进来先关掉全部 DO */
    for (size_t i = 0; i < 26; ++i)
        TURN_OFF(i);

    /* 读取 DI，必要时可作暂停/急停等判断 */
    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); ++i)
    {
        di_level[i] = get_di_pin(i);
    }

    /*================ 主循环 ================*/
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

        /*========== 状态机 ==========*/
        if (status == 0)
        { /* 1 放冷水 5 s */
            if (time_cnt == 0)
            {
                TURN_ON(7);
                GROUP_ON(12, 21);
                TURN_ON(8);
            }
            if (++time_cnt >= 5)
            {
                status = 1;
                time_cnt = 0;
            }
        }
        else if (status == 1)
        { /* 2 冲水 40 s */
            if (time_cnt == 0)
            {
                TURN_ON(3);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 40)
            {
                status = 2;
                time_cnt = 0;
            }
        }
        else if (status == 2)
        { /* 3 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                GROUP_OFF(12, 21);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 3;
                time_cnt = 0;
            }
        }
        else if (status == 3)
        { /* 4 洗发水 10 s */
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
            if (++time_cnt >= 10)
            {
                status = 4;
                time_cnt = 0;
            }
        }
        else if (status == 4)
        { /* 5 暂停 10 s */
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
        { /* 6 冲水 50 s */
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
            if (++time_cnt >= 50)
            {
                status = 6;
                time_cnt = 0;
            }
        }
        else if (status == 6)
        { /* 7 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 7;
                time_cnt = 0;
            }
        }
        else if (status == 7)
        { /* 8 洗发水 10 s */
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 8;
                time_cnt = 0;
            }
        }
        else if (status == 8)
        { /* 9 暂停 10 s */
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
        }
        else if (status == 9)
        { /* 10 冲水 50 s */
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
            if (++time_cnt >= 50)
            {
                status = 10;
                time_cnt = 0;
            }
        }
        else if (status == 10)
        { /* 11 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 11;
                time_cnt = 0;
            }
        }
        else if (status == 11)
        { /* 12 护发素 10 s */
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
            if (++time_cnt >= 10)
            {
                status = 12;
                time_cnt = 0;
            }
        }
        else if (status == 12)
        { /* 13 暂停 10 s */
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
        }
        else if (status == 13)
        { /* 14 冲水 50 s */
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
            if (++time_cnt >= 50)
            {
                status = 14;
                time_cnt = 0;
            }
        }
        else if (status == 14)
        { /* 15 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 15;
                time_cnt = 0;
            }
        }
        else if (status == 15)
        { /* 16 洗发水 10 s */
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 16;
                time_cnt = 0;
            }
        }
        else if (status == 16)
        { /* 17 暂停 10 s */
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
        }
        else if (status == 17)
        { /* 18 冲水 50 s */
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
            if (++time_cnt >= 50)
            {
                status = 18;
                time_cnt = 0;
            }
        }
        else if (status == 18)
        { /* 19 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 19;
                time_cnt = 0;
            }
        }
        else if (status == 19)
        { /* 20 护发素 10 s */
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
            if (++time_cnt >= 10)
            {
                status = 20;
                time_cnt = 0;
            }
        }
        else if (status == 20)
        { /* 21 暂停 10 s */
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
        }
        else if (status == 21)
        { /* 22 冲水 50 s */
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
            if (++time_cnt >= 50)
            {
                status = 22;
                time_cnt = 0;
            }
        }
        else if (status == 22)
        { /* 23 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 23;
                time_cnt = 0;
            }
        }
        else if (status == 23)
        { /* 24 洗发水 10 s */
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 24;
                time_cnt = 0;
            }
        }
        else if (status == 24)
        { /* 25 暂停 10 s */
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
        }
        else if (status == 25)
        { /* 26 冲水 60 s */
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
            if (++time_cnt >= 60)
            {
                status = 26;
                time_cnt = 0;
            }
        }
        else if (status == 26)
        { /* 27 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 27;
                time_cnt = 0;
            }
        }
        else if (status == 27)
        { /* 28 护发素 10 s */
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
            if (++time_cnt >= 10)
            {
                status = 28;
                time_cnt = 0;
            }
        }
        else if (status == 28)
        { /* 29 暂停 10 s */
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
        }
        else if (status == 29)
        { /* 30 冲水 60 s */
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
            if (++time_cnt >= 60)
            {
                status = 30;
                time_cnt = 0;
            }
        }
        else if (status == 30)
        { /* 31 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 31;
                time_cnt = 0;
            }
        }
        else if (status == 31)
        { /* 32 洗发水 10 s */
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 32;
                time_cnt = 0;
            }
        }
        else if (status == 32)
        { /* 33 暂停 10 s */
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
                status = 33;
                time_cnt = 0;
            }
        }
        else if (status == 33)
        { /* 34 冲水 60 s */
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
            if (++time_cnt >= 60)
            {
                status = 34;
                time_cnt = 0;
            }
        }
        else if (status == 34)
        { /* 35 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 35;
                time_cnt = 0;
            }
        }
        else if (status == 35)
        { /* 36 护发素 10 s */
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
            if (++time_cnt >= 10)
            {
                status = 36;
                time_cnt = 0;
            }
        }
        else if (status == 36)
        { /* 37 暂停 10 s */
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
                status = 37;
                time_cnt = 0;
            }
        }
        else if (status == 37)
        { /* 38 冲水 60 s */
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
            if (++time_cnt >= 60)
            {
                status = 38;
                time_cnt = 0;
            }
        }
        else if (status == 38)
        { /* 39 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 39;
                time_cnt = 0;
            }
        }
        else if (status == 39)
        { /* 40 洗发水 10 s */
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                TURN_ON(16);
                TURN_ON(5);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 10)
            {
                status = 40;
                time_cnt = 0;
            }
        }
        else if (status == 40)
        { /* 41 暂停 10 s */
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
                status = 41;
                time_cnt = 0;
            }
        }
        else if (status == 41)
        { /* 42 冲水 60 s */
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
            if (++time_cnt >= 60)
            {
                status = 42;
                time_cnt = 0;
            }
        }
        else if (status == 42)
        { /* 43 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 43;
                time_cnt = 0;
            }
        }
        else if (status == 43)
        { /* 44 护发素 10 s */
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
            if (++time_cnt >= 10)
            {
                status = 44;
                time_cnt = 0;
            }
        }
        else if (status == 44)
        { /* 45 暂停 10 s */
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
                status = 45;
                time_cnt = 0;
            }
        }
        else if (status == 45)
        { /* 46 冲水 60 s */
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
            if (++time_cnt >= 60)
            {
                status = 46;
                time_cnt = 0;
            }
        }
        else if (status == 46)
        { /* 47 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 47;
                time_cnt = 0;
            }
        }
        else if (status == 47)
        { /* 48 护发素 10 s */
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
            if (++time_cnt >= 10)
            {
                status = 48;
                time_cnt = 0;
            }
        }
        else if (status == 48)
        { /* 49 暂停 10 s */
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
                status = 49;
                time_cnt = 0;
            }
        }
        else if (status == 49)
        { /* 50 冲水 60 s */
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
            if (++time_cnt >= 60)
            {
                status = 50;
                time_cnt = 0;
            }
        }
        else if (status == 50)
        { /* 51 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 51;
                time_cnt = 0;
            }
        }
        else if (status == 51)
        { /* 52 护发素 10 s */
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
            if (++time_cnt >= 10)
            {
                status = 52;
                time_cnt = 0;
            }
        }
        else if (status == 52)
        { /* 53 暂停 10 s */
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
                status = 53;
                time_cnt = 0;
            }
        }
        else if (status == 53)
        { /* 54 冲水 60 s */
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
            if (++time_cnt >= 60)
            {
                status = 54;
                time_cnt = 0;
            }
        }
        else if (status == 54)
        { /* 55 暂停 10 s */
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                GROUP_OFF(12, 21);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 55;
                time_cnt = 0;
            }
        }
        else if (status == 55)
        { /* 56 结束 */
            TURN_OFF(10);
            TURN_OFF(12);
            ESP_LOGI(TAG, "finished, runtime = %ds", runtime);
            return 0;
        }

        /*========== 1 s Tick ==========*/
        delay_1s();
        ++runtime;
    }
}
