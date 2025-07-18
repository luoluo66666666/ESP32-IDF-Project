#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_ctrl.h"

static const char *TAG = "MODE5";

extern int do_pin[26];
extern int di_pin[6];

/*---------------------------- GPIO 操作简写 ----------------------------*/
#define TURN_ON(p) set_do_pin((p), 1)
#define TURN_OFF(p) set_do_pin((p), 0)
#define TOGGLE(p) set_do_pin((p), !get_do_pin((p)))

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

static inline void delay_1s(void) { vTaskDelay(pdMS_TO_TICKS(1000)); }

/*======================================================================
 *  mode5_up : 4 min 快洗（头部）
 *====================================================================*/
int mode5_up(void)
{
    int status = 0, time_cnt = 0, runtime = 0;
    int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};

    ESP_LOGI(TAG, "Entering mode5_up");

    for (int i = 0; i < 26; ++i)
        TURN_OFF(i);

    /* 读取 DI，必要时可作暂停/急停等判断 */
    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); ++i)
    {
        di_level[i] = get_di_pin(i);
    }

    while (true)
    {
        ESP_LOGI(TAG, "mode5_up status:%d time_cnt:%d", status, time_cnt);

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
            /*──────── 0 : 放冷水 5 s ────────*/
        case 0:
            if (time_cnt == 0)
            {
                TURN_ON(7);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
                TURN_ON(8);
            }
            if (++time_cnt >= 5)
            {
                TURN_OFF(8);
                status = 1;
                time_cnt = 0;
            }
            break;

            /*──────── 1 : 冲水 15 s ────────*/
        case 1:
            if (time_cnt == 0)
            {
                TURN_ON(3);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 15)
            {
                status = 2;
                time_cnt = 0;
            }
            break;

            /*──────── 2 : 暂停 8 s ────────*/
        case 2:
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 8)
            {
                status = 3;
                time_cnt = 0;
            }
            break;

            /*──────── 3 : 洗发水 5 s ───────*/
        case 3:
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
            break;

            /*──────── 4 : 暂停 8 s ────────*/
        case 4:
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(5);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 8)
            {
                status = 5;
                time_cnt = 0;
            }
            break;

            /*──────── 5 : 冲水 15 s ────────*/
        case 5:
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 15)
            {
                status = 6;
                time_cnt = 0;
            }
            break;

            /*──────── 6 : 暂停 8 s ────────*/
        case 6:
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 8)
            {
                status = 7;
                time_cnt = 0;
            }
            break;

            /*──────── 7 : 洗发水 5 s ───────*/
        case 7:
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
            break;

            /*──────── 8 : 暂停 8 s ────────*/
        case 8:
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(5);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 8)
            {
                status = 9;
                time_cnt = 0;
            }
            break;

            /*──────── 9 : 冲水 15 s ────────*/
        case 9:
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 15)
            {
                status = 10;
                time_cnt = 0;
            }
            break;

            /*──────── 10 : 暂停 8 s ───────*/
        case 10:
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 8)
            {
                status = 11;
                time_cnt = 0;
            }
            break;

            /*──────── 11 : 护发素 5 s ───────*/
        case 11:
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
                status = 12;
                time_cnt = 0;
            }
            break;

            /*──────── 12 : 暂停 8 s ───────*/
        case 12:
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(6);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 8)
            {
                status = 13;
                time_cnt = 0;
            }
            break;

            /*──────── 13 : 冲水 20 s ───────*/
        case 13:
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 20)
            {
                status = 14;
                time_cnt = 0;
            }
            break;

            /*──────── 14 : 暂停 8 s ───────*/
        case 14:
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 8)
            {
                status = 15;
                time_cnt = 0;
            }
            break;

            /*──────── 15 : 洗发水 5 s ───────*/
        case 15:
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
            break;

            /*──────── 16 : 暂停 8 s ───────*/
        case 16:
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(5);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 8)
            {
                status = 17;
                time_cnt = 0;
            }
            break;

            /*──────── 17 : 冲水 30 s ───────*/
        case 17:
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 30)
            {
                status = 18;
                time_cnt = 0;
            }
            break;

            /*──────── 18 : 暂停 8 s ───────*/
        case 18:
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 8)
            {
                status = 19;
                time_cnt = 0;
            }
            break;

            /*──────── 19 : 护发素 5 s ───────*/
        case 19:
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
            break;

            /*──────── 20 : 暂停 8 s (DO11 ON) ───────*/
        case 20:
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                TURN_OFF(16);
                TURN_OFF(6);
                TURN_OFF(0);
                TURN_OFF(1);
                TURN_ON(11);
            }
            if (++time_cnt >= 8)
            {
                status = 21;
                time_cnt = 0;
            }
            break;

            /*──────── 21 : 冲水 30 s (冷水泵 DO7) ─────*/
        case 21:
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
                TURN_ON(0);
                TURN_OFF(1);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 30)
            {
                status = 22;
                time_cnt = 0;
            }
            break;

            /*──────── 22 : 结束冲水 – DO10 ON 1 s ─────*/
        case 22:
            if (time_cnt == 0)
            {
                TURN_ON(10);
                TURN_OFF(7);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 1)
            {
                status = 23;
                time_cnt = 0;
            }
            break;

            /*──────── 23 : 关闭 DO10 → 结束 ───────────*/
        case 23:
            if (time_cnt == 0)
                TURN_OFF(10);
            if (++time_cnt >= 1)
            {
                ESP_LOGI(TAG, "mode5_up finished, runtime=%d s", runtime + 1);
                return 0;
            }
            break;

        default:
            ESP_LOGE(TAG, "Unknown state %d", status);
            return -1;
        }

        delay_1s();
        ++runtime;
    }
}

/*======================================================================
 *  mode5_down : 6 min 快洗（下半流程）
 *====================================================================*/
int mode5_down(void)
{
    int status = 0, time_cnt = 0, runtime = 0;
    int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};

    ESP_LOGI(TAG, "Entering mode5_down");
    for (int i = 0; i < 26; ++i)
        TURN_OFF(i);

    /* 读取 DI，必要时可作暂停/急停等判断 */
    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); ++i)
    {
        di_level[i] = get_di_pin(i);
    }

    while (true)
    {
        ESP_LOGI(TAG, "mode5_down status:%d time_cnt:%d", status, time_cnt);

        /* 暂停键，放在这里检测 (await_pause_and_restore) */
        if (get_di_pin(2) == 1) 
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
            /*──────── 0 : 放冷水 5 s ────────*/
        case 0:
            if (time_cnt == 0)
            {
                TURN_ON(7);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
                TURN_ON(8);
            }
            if (++time_cnt >= 5)
            {
                TURN_OFF(8);
                status = 1;
                time_cnt = 0;
            }
            break;

            /*──────── 1 : 冲水 30 s ────────*/
        case 1:
            if (time_cnt == 0)
            {
                TURN_ON(3);
                TURN_ON(0);
            }
            TOGGLE(0);
            TOGGLE(1);
            if (++time_cnt >= 30)
            {
                status = 2;
                time_cnt = 0;
            }
            break;

            /*──────── 2 : 暂停 10 s ───────*/
        case 2:
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                TURN_OFF(0);
                TURN_OFF(1);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
            }
            if (++time_cnt >= 10)
            {
                status = 3;
                time_cnt = 0;
            }
            break;

            /*──────── 3 : 洗发水 5 s ───────*/
        case 3:
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
            break;

            /*──────── 4 : 暂停 10 s ───────*/
        case 4:
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

            /*──────── 5 : 冲水 30 s ────────*/
        case 5:
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
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
            break;

            /*──────── 6 : 暂停 10 s ───────*/
        case 6:
            if (time_cnt == 0)
            {
                TURN_OFF(7);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 7;
                time_cnt = 0;
            }
            break;

            /*──────── 7 : 洗发水 5 s (DO9) ───────*/
        case 7:
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
            if (++time_cnt >= 5)
            {
                status = 8;
                time_cnt = 0;
            }
            break;

            /*──────── 8 : 暂停 10 s ───────*/
        case 8:
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

            /*──────── 9 : 冲水 30 s (DO9) ───────*/
        case 9:
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
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
            break;

            /*──────── 10 : 暂停 10 s ───────*/
        case 10:
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 11;
                time_cnt = 0;
            }
            break;

            /*──────── 11 : 护发素 5 s ───────*/
        case 11:
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
                status = 12;
                time_cnt = 0;
            }
            break;

            /*──────── 12 : 暂停 10 s ───────*/
        case 12:
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

            /*──────── 13 : 冲水 30 s ───────*/
        case 13:
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
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
            break;

            /*──────── 14 : 暂停 10 s ───────*/
        case 14:
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 15;
                time_cnt = 0;
            }
            break;

            /*──────── 15 : 洗发水 5 s ───────*/
        case 15:
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
            if (++time_cnt >= 5)
            {
                status = 16;
                time_cnt = 0;
            }
            break;

            /*──────── 16 : 暂停 10 s ───────*/
        case 16:
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

            /*──────── 17 : 冲水 40 s ───────*/
        case 17:
            if (time_cnt == 0)
            {
                TURN_ON(9);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
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
            break;

            /*──────── 18 : 暂停 10 s ───────*/
        case 18:
            if (time_cnt == 0)
            {
                TURN_OFF(9);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 19;
                time_cnt = 0;
            }
            break;

            /*──────── 19 : 护发素 5 s ───────*/
        case 19:
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
            break;

            /*──────── 20 : 暂停 10 s ───────*/
        case 20:
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

            /*──────── 21 : 冲水 60 s (DO7) ───────*/
        case 21:
            if (time_cnt == 0)
            {
                TURN_ON(7);
                TURN_ON(3);
                for (int i = 12; i < 22; ++i)
                    TURN_ON(i);
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
            break;

            /*──────── 22 : 暂停 10 s DO10 ON ───────*/
        case 22:
            if (time_cnt == 0)
            {
                TURN_ON(10);
                TURN_OFF(7);
                TURN_OFF(3);
                for (int i = 12; i < 22; ++i)
                    TURN_OFF(i);
                TURN_OFF(0);
                TURN_OFF(1);
            }
            if (++time_cnt >= 10)
            {
                status = 23;
                time_cnt = 0;
            }
            break;

            /*──────── 23 : 结束 ───────*/
        case 23:
            if (time_cnt == 0)
                TURN_OFF(10);
            if (++time_cnt >= 1)
            {
                ESP_LOGI(TAG, "mode5_down finished, runtime=%d s", runtime + 1);
                return 0;
            }
            break;

        default:
            ESP_LOGE(TAG, "Unknown state %d", status);
            return -1;
        }

        delay_1s();
        ++runtime;
    }
}
