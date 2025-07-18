#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_ctrl.h"

static const char *TAG = "MODE3";

extern int do_pin[26];
extern int di_pin[6];

/*-------------------------------------------------
 * GPIO 操作小工具（保持可读性，但不折叠流程逻辑）
 *------------------------------------------------*/
#define TURN_ON(p)      set_do_pin((p), 1)
#define TURN_OFF(p)     set_do_pin((p), 0)
#define TOGGLE(p)       set_do_pin((p), !get_do_pin((p)))

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

/*-------------------------------------------------
 * mode3 : 25 分钟护发洗（72 个显式状态）
 *------------------------------------------------*/
int mode3(void)
{
    int status   = 0;   /* 当前状态编号 */
    int time_cnt = 0;   /* 当前状态累计秒数 */
    int runtime  = 0;   /* 总运行秒数     */
        int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};

    ESP_LOGI(TAG, "Entering mode 3");

    /* 上电全部关 DO */
    for (size_t i = 0; i < 26; ++i) TURN_OFF(i);

        /* 读取 DI，必要时可作暂停/急停等判断 */
    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); ++i)
    {
        di_level[i] = get_di_pin(i);
    }
    /*================ 主循环 ================*/
    while (true) {

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

        /*——————————————————————
         * 逐状态显式流程控制
         *——————————————————————*/
        switch (status)
        {
        /*---------- 0 : 放冷水 5 s ----------*/
        case 0:
            if (time_cnt == 0) {
                TURN_ON(7);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(8);
            }
            if (++time_cnt >= 5) {
                TURN_OFF(8);          /* 5 秒后关 DO8 */
                status = 1;  time_cnt = 0;
            }
            break;

        /*---------- 1 : 冲水 40 s ----------*/
        case 1:
            if (time_cnt == 0) {
                TURN_ON(3); TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 40) { status = 2; time_cnt = 0; }
            break;

        /*---------- 2 : 暂停 10 s ----------*/
        case 2:
            if (time_cnt == 0) {
                TURN_OFF(7);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(3); TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 3; time_cnt = 0; }
            break;

        /*---------- 3 : 洗发液 10 s ----------*/
        case 3:
            if (time_cnt == 0) {
                TURN_ON(7); TURN_ON(3); TURN_ON(16); TURN_ON(5);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 4; time_cnt = 0; }
            break;

        /*---------- 4 : 暂停 10 s ----------*/
        case 4:
            if (time_cnt == 0) {
                TURN_OFF(7); TURN_OFF(3); TURN_OFF(16); TURN_OFF(5);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 5; time_cnt = 0; }
            break;

        /*---------- 5 : 冲水 50 s ----------*/
        case 5:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 50) { status = 6; time_cnt = 0; }
            break;

        /*---------- 6 : 暂停 10 s ----------*/
        case 6:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 7; time_cnt = 0; }
            break;

        /*---------- 7 : 洗发水 10 s ----------*/
        case 7:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(5);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 8; time_cnt = 0; }
            break;

        /*---------- 8 : 暂停 10 s ----------*/
        case 8:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(5);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 9; time_cnt = 0; }
            break;

        /*---------- 9 : 冲水 50 s ----------*/
        case 9:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 50) { status = 10; time_cnt = 0; }
            break;

        /*---------- 10 : 暂停 10 s ----------*/
        case 10:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 11; time_cnt = 0; }
            break;

        /*---------- 11 : 洗发水 10 s ----------*/
        case 11:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(6);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 12; time_cnt = 0; }
            break;

        /*---------- 12 : 暂停 10 s ----------*/
        case 12:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(6);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 13; time_cnt = 0; }
            break;

        /*---------- 13 : 冲水 50 s ----------*/
        case 13:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 50) { status = 14; time_cnt = 0; }
            break;

        /*---------- 14 : 暂停 10 s ----------*/
        case 14:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 15; time_cnt = 0; }
            break;

        /*---------- 15 : 洗发液 10 s ----------*/
        case 15:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(5);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 16; time_cnt = 0; }
            break;

        /*---------- 16 : 暂停 10 s ----------*/
        case 16:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(5);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 17; time_cnt = 0; }
            break;

        /*---------- 17 : 冲水 50 s ----------*/
        case 17:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 50) { status = 18; time_cnt = 0; }
            break;

        /*---------- 18 : 暂停 10 s ----------*/
        case 18:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 19; time_cnt = 0; }
            break;

        /*---------- 19 : 护发素 10 s ----------*/
        case 19:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(6);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 20; time_cnt = 0; }
            break;

        /*---------- 20 : 暂停 10 s ----------*/
        case 20:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(6);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 21; time_cnt = 0; }
            break;

        /*---------- 21 : 冲水 50 s ----------*/
        case 21:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 50) { status = 22; time_cnt = 0; }
            break;

        /*---------- 22 : 暂停 10 s ----------*/
        case 22:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 23; time_cnt = 0; }
            break;

        /*---------- 23 : 洗发液 10 s ----------*/
        case 23:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(5);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 24; time_cnt = 0; }
            break;

        /*---------- 24 : 暂停 10 s ----------*/
        case 24:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(5);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 25; time_cnt = 0; }
            break;

        /*---------- 25 : 冲水 60 s ----------*/
        case 25:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 26; time_cnt = 0; }
            break;

        /*---------- 26 : 暂停 10 s ----------*/
        case 26:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 27; time_cnt = 0; }
            break;

        /*---------- 27 : 护发素 10 s ----------*/
        case 27:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(6);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 28; time_cnt = 0; }
            break;

        /*---------- 28 : 暂停 10 s ----------*/
        case 28:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(6);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 29; time_cnt = 0; }
            break;

        /*---------- 29 : 冲水 60 s ----------*/
        case 29:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 30; time_cnt = 0; }
            break;

        /*---------- 30 : 暂停 10 s ----------*/
        case 30:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 31; time_cnt = 0; }
            break;

        /*---------- 31 : 洗发液 10 s ----------*/
        case 31:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(5);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 32; time_cnt = 0; }
            break;

        /*---------- 32 : 暂停 10 s ----------*/
        case 32:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(5);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 33; time_cnt = 0; }
            break;

        /*---------- 33 : 冲水 60 s ----------*/
        case 33:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 34; time_cnt = 0; }
            break;

        /*---------- 34 : 暂停 10 s ----------*/
        case 34:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 35; time_cnt = 0; }
            break;

        /*---------- 35 : 护发素 10 s ----------*/
        case 35:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(6);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 36; time_cnt = 0; }
            break;

        /*---------- 36 : 暂停 10 s ----------*/
        case 36:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(6);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 37; time_cnt = 0; }
            break;

        /*---------- 37 : 冲水 60 s ----------*/
        case 37:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 38; time_cnt = 0; }
            break;

        /*---------- 38 : 暂停 10 s ----------*/
        case 38:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 39; time_cnt = 0; }
            break;

        /*---------- 39 : 洗发液 10 s ----------*/
        case 39:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(5);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 40; time_cnt = 0; }
            break;

        /*---------- 40 : 暂停 10 s ----------*/
        case 40:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(5);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 41; time_cnt = 0; }
            break;

        /*---------- 41 : 冲水 60 s ----------*/
        case 41:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 42; time_cnt = 0; }
            break;

        /*---------- 42 : 暂停 10 s ----------*/
        case 42:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 43; time_cnt = 0; }
            break;

        /*---------- 43 : 护发素 10 s ----------*/
        case 43:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(6);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 44; time_cnt = 0; }
            break;

        /*---------- 44 : 暂停 10 s ----------*/
        case 44:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(6);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 45; time_cnt = 0; }
            break;

        /*---------- 45 : 冲水 60 s ----------*/
        case 45:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 46; time_cnt = 0; }
            break;

        /*---------- 46 : 暂停 10 s ----------*/
        case 46:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 47; time_cnt = 0; }
            break;

        /*---------- 47 : 洗发液 10 s ----------*/
        case 47:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(5);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 48; time_cnt = 0; }
            break;

        /*---------- 48 : 暂停 10 s ----------*/
        case 48:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(5);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 49; time_cnt = 0; }
            break;

        /*---------- 49 : 冲水 60 s ----------*/
        case 49:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 50; time_cnt = 0; }
            break;

        /*---------- 50 : 暂停 10 s ----------*/
        case 50:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 51; time_cnt = 0; }
            break;

        /*---------- 51 : 护发素 10 s ----------*/
        case 51:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(6);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 52; time_cnt = 0; }
            break;

        /*---------- 52 : 暂停 10 s ----------*/
        case 52:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(6);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 53; time_cnt = 0; }
            break;

        /*---------- 53 : 冲水 60 s ----------*/
        case 53:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 54; time_cnt = 0; }
            break;

        /*---------- 54 : 暂停 10 s ----------*/
        case 54:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 55; time_cnt = 0; }
            break;

        /*---------- 55 : 洗发水 10 s ----------*/
        case 55:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(5);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 56; time_cnt = 0; }
            break;

        /*---------- 56 : 暂停 10 s ----------*/
        case 56:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(5);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 57; time_cnt = 0; }
            break;

        /*---------- 57 : 冲水 60 s ----------*/
        case 57:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 58; time_cnt = 0; }
            break;

        /*---------- 58 : 暂停 10 s ----------*/
        case 58:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 59; time_cnt = 0; }
            break;

        /*---------- 59 : 护发素 10 s ----------*/
        case 59:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(6);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 60; time_cnt = 0; }
            break;

        /*---------- 60 : 暂停 10 s ----------*/
        case 60:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(6);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 61; time_cnt = 0; }
            break;

        /*---------- 61 : 冲水 60 s ----------*/
        case 61:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 62; time_cnt = 0; }
            break;

        /*---------- 62 : 暂停 10 s ----------*/
        case 62:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 63; time_cnt = 0; }
            break;

        /*---------- 63 : 护发素 10 s ----------*/
        case 63:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(6);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 64; time_cnt = 0; }
            break;

        /*---------- 64 : 暂停 10 s ----------*/
        case 64:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(6);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 65; time_cnt = 0; }
            break;

        /*---------- 65 : 冲水 60 s ----------*/
        case 65:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 66; time_cnt = 0; }
            break;

        /*---------- 66 : 暂停 10 s ----------*/
        case 66:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 67; time_cnt = 0; }
            break;

        /*---------- 67 : 护发素 10 s ----------*/
        case 67:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3); TURN_ON(16); TURN_ON(6);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 10) { status = 68; time_cnt = 0; }
            break;

        /*---------- 68 : 暂停 10 s ----------*/
        case 68:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3); TURN_OFF(16); TURN_OFF(6);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 69; time_cnt = 0; }
            break;

        /*---------- 69 : 冲水 60 s ----------*/
        case 69:
            if (time_cnt == 0) {
                TURN_ON(9); TURN_ON(3);
                for (int i = 12; i < 22; ++i) TURN_ON(i);
                TURN_ON(0); TURN_OFF(1);
            }
            TOGGLE(0); TOGGLE(1);
            if (++time_cnt >= 60) { status = 70; time_cnt = 0; }
            break;

        /*---------- 70 : 暂停 10 s ----------*/
        case 70:
            if (time_cnt == 0) {
                TURN_OFF(9); TURN_OFF(3);
                for (int i = 12; i < 22; ++i) TURN_OFF(i);
                TURN_OFF(0); TURN_OFF(1);
            }
            if (++time_cnt >= 10) { status = 71; time_cnt = 0; }
            break;

        /*---------- 71 : 结束 ----------*/
        case 71:
            if (time_cnt == 0) {
                TURN_OFF(10); TURN_OFF(12);
            }
            if (++time_cnt >= 1) {
                ESP_LOGI(TAG, "finished, runtime = %d s", runtime + 1);
                return 0;          /* 流程完成，返回主程序 */
            }
            break;

        default:
            /* 不应到达 */
            ESP_LOGE(TAG, "Unknown state %d - abort", status);
            return -1;
        }

        /* 每秒调用一次 */
        delay_1s();
        ++runtime;
    }
}
