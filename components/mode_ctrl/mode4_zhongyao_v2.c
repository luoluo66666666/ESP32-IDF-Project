#include "ctrl_protocol.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mode_ctrl.h"
#include "temp.h"

static const char *TAG = "MODE4_V2";

#define MODE4_DEFAULT_TARGET_TEMP 39 // 默认目标温度
#define MODE4_TEMP_TOLERANCE 5
#define MODE4_LIGHT_GPIO GPIO_NUM_19 // 彩灯连接的 GPIO
#define MODE4_DEBUG_ENABLE 1

#if MODE4_DEBUG_ENABLE
#define MODE4_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define MODE4_LOGI(...)
#endif

extern int do_pin[26];
extern int di_pin[6];

/* 初始化彩灯引脚 */
static void mode4_light_init(void)
{
    esp_rom_gpio_pad_select_gpio(MODE4_LIGHT_GPIO);
    ESP_ERROR_CHECK(gpio_reset_pin(MODE4_LIGHT_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(MODE4_LIGHT_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(MODE4_LIGHT_GPIO, 0));
}

/* 关闭全部 DO 输出 */
static void mode4_all_off_enter(void)
{
    for (size_t i = 0; i < 26; ++i)
    {
        TURN_OFF(i);
    }
}

/* 第一步放水：打开清水阀、两个出水阀和头汤阀 */
static void mode4_cold_enter(void)
{
    TURN_ON(7);
    TURN_ON(13);
    TURN_ON(14);
    TURN_ON(8);
}

/* 普通放水：打开高压水泵、清水阀和两个出水阀 */
static void mode4_rinse_enter(void)
{
    TURN_ON(3);
    TURN_ON(7);
    TURN_ON(13);
    TURN_ON(14);
}

/* 暂停：关闭水泵、阀门和加液输出 */
static void mode4_pause_enter(void)
{
    motor_stop();
    TURN_OFF(3);
    TURN_OFF(5);
    TURN_OFF(6);
    TURN_OFF(7);
    TURN_OFF(8);
    TURN_OFF(12);
    TURN_OFF(13);
    TURN_OFF(14);
}

/* 中药：打开高压水泵、清水阀、中药泵和两个出水阀 */
static void mode4_zhongyao_enter(void)
{
    TURN_ON(3);
    TURN_ON(7);
    TURN_ON(12);
    TURN_ON(13);
    TURN_ON(14);
}

/* 洗发水：打开高压水泵、清水阀、洗发水泵和两个出水阀 */
static void mode4_shampoo_enter(void)
{
    TURN_ON(3);
    TURN_ON(7);
    TURN_ON(5);
    TURN_ON(13);
    TURN_ON(14);
}

/* 护发素：打开高压水泵、清水阀、护发素泵和两个出水阀 */
static void mode4_conditioner_enter(void)
{
    TURN_ON(3);
    TURN_ON(7);
    TURN_ON(6);
    TURN_ON(13);
    TURN_ON(14);
}

/* 最终冲水：打开高压水泵、清水阀和两个出水阀 */
static void mode4_final_rinse_enter(void)
{
    TURN_ON(3);
    TURN_ON(7);
    TURN_ON(13);
    TURN_ON(14);
}

/* 排水：关闭水路并打开排水阀 */
static void mode4_drain_enter(void)
{
    motor_stop();
    TURN_OFF(3);
    TURN_OFF(5);
    TURN_OFF(6);
    TURN_OFF(7);
    TURN_OFF(8);
    TURN_OFF(12);
    TURN_OFF(13);
    TURN_OFF(14);
    TURN_ON(25);
}

/* 启动撑杆电机运动 */
static void mode4_motor_run_enter(void)
{
    motor_run();
}

/* 停止撑杆电机运动 */
static void mode4_motor_stop_enter(void)
{
    motor_stop();
}

/* 处理暂停，恢复前保持当前 DO 状态 */
static void mode4_handle_pause(int do_level[], size_t do_count)
{
    if (get_di_pin(2) != 1)
    {
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "Paused");
    for (size_t i = 0; i < do_count; ++i)
    {
        do_level[i] = get_do_pin(i);
        TURN_OFF(i);
    }

    while (get_di_pin(2) == 1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    for (size_t i = 0; i < do_count; ++i)
    {
        set_do_pin(i, do_level[i]);
    }
    ESP_LOGI(TAG, "Resumed");
}

/* 运行步骤计时，到时后停止电机并跳转到下一状态 */
static bool mode4_active_step_tick(int *time_cnt, int limit, int *status, int next_status)
{
    if (mode_stop_requested())
    {
        mode4_motor_stop_enter();
        *time_cnt = 0;
        return true;
    }

    ++(*time_cnt);
    if (*time_cnt >= limit)
    {
        mode4_motor_stop_enter();
        *status   = next_status;
        *time_cnt = 0;
        return true;
    }
    return false;
}

/* 判断当前状态是否需要等待温度 */
static bool mode4_need_wait_temperature(int status)
{
    switch (status)
    {
    case 1:
    case 3:
    case 5:
    case 7:
    case 9:
    case 11:
    case 13:
    case 15:
    case 17:
    case 19:
    case 21:
    case 23:
    case 25:
    case 27:
    case 29:
    case 31:
    case 33:
    case 35:
    case 37:
    case 39:
    case 41:
    case 43:
    case 45:
    case 47:
    case 49:
    case 51:
    case 53:
    case 55:
    case 57:
    case 59:
    case 61:
    case 63:
    case 65:
    case 67:
        return true;
    default:
        return false;
    }
}

/* 等待出水温度进入目标范围 */
static void mode4_wait_temperature_ready(int do_level[], size_t do_count)
{
    uint16_t water_temp  = 0;
    uint16_t target_temp = MODE4_DEFAULT_TARGET_TEMP;
    uint16_t min_temp    = 0;
    uint16_t max_temp    = 0;

    while (true)
    {
        if (mode_stop_requested())
        {
            return;
        }

        mode4_handle_pause(do_level, do_count);

        if (temp_read_target_temperature(&target_temp) != ESP_OK || target_temp == 0)
        {
            target_temp = MODE4_DEFAULT_TARGET_TEMP;
        }

        min_temp = target_temp - MODE4_TEMP_TOLERANCE;
        max_temp = target_temp + MODE4_TEMP_TOLERANCE;

        if (temp_read_water_temperature(&water_temp) == ESP_OK)
        {
            if (water_temp >= min_temp && water_temp <= max_temp)
            {
                ESP_LOGI(TAG, "temperature ready: %u", water_temp);
                return;
            }
            ESP_LOGI(TAG, "waiting temperature: current=%u target=%d range=[%u,%u]",
                     water_temp, target_temp, min_temp, max_temp);
        }
        else
        {
            ESP_LOGW(TAG, "read water temperature failed, retry");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* 这里按 100ms 为一个计数单位，10 代表 1.0s */
int mode4_zhongyao_v2(void)
{
    int status                                       = 0;
    int time_cnt                                     = 0;
    int runtime                                      = 0;
    int di_level[sizeof(di_pin) / sizeof(di_pin[0])] = {0};
    int do_level[sizeof(do_pin) / sizeof(do_pin[0])] = {0};

    ESP_LOGI(TAG, "Entering mode4_zhongyao_v2");

    mode4_light_init();
    gpio_set_level(MODE4_LIGHT_GPIO, 1);
    mode4_all_off_enter();

    for (size_t i = 0; i < sizeof(di_pin) / sizeof(di_pin[0]); ++i)
    {
        di_level[i] = get_di_pin(i);
    }

    while (true)
    {
        if (time_cnt == 0)
        {
            MODE4_LOGI("enter status:%d", status);
        }

        if (mode_stop_requested())
        {
            ESP_LOGW(TAG, "mode stop requested");
            mode4_all_off_enter();
            mode4_motor_stop_enter();
            gpio_set_level(MODE4_LIGHT_GPIO, 0);
            TURN_OFF(25);
            clear_mode_stop_request();
            return -1;
        }

        mode4_handle_pause(do_level, sizeof(do_pin) / sizeof(do_pin[0]));
        TURN_ON(11);

        // if (time_cnt == 0 && mode4_need_wait_temperature(status))
        // {
        //     mode4_wait_temperature_ready(do_level, sizeof(do_pin) / sizeof(do_pin[0]));
        // }

        switch (status)
        {
        case 0: /* 放水 5.0s */
            if (time_cnt == 0)
            {
                mode4_cold_enter();
            }
            if (++time_cnt >= 50)
            {
                TURN_OFF(8);
                status   = 1;
                time_cnt = 0;
            }
            break;

        case 1: /* 冲水 40.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 400, &status, 2))
                break;
            break;

        case 2: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 3;
                time_cnt = 0;
            }
            break;

        case 3: /* 中药 2.1s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 21, &status, 4))
                break;
            break;

        case 4: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 5;
                time_cnt = 0;
            }
            break;

        case 5: /* 冲水 50.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 500, &status, 6))
                break;
            break;

        case 6: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 7;
                time_cnt = 0;
            }
            break;

        case 7: /* 中药 2.1s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 21, &status, 8))
                break;
            break;

        case 8: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 9;
                time_cnt = 0;
            }
            break;

        case 9: /* 冲水 50.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 500, &status, 10))
                break;
            break;

        case 10: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 11;
                time_cnt = 0;
            }
            break;

        case 11: /* 中药 2.1s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 21, &status, 12))
                break;
            break;

        case 12: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 13;
                time_cnt = 0;
            }
            break;

        case 13: /* 冲水 50.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 500, &status, 14))
                break;
            break;

        case 14: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 15;
                time_cnt = 0;
            }
            break;

        case 15: /* 中药 1.8s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 18, &status, 16))
                break;
            break;

        case 16: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 17;
                time_cnt = 0;
            }
            break;

        case 17: /* 冲水 50.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 500, &status, 18))
                break;
            break;

        case 18: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 19;
                time_cnt = 0;
            }
            break;

        case 19: /* 中药 1.5s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 15, &status, 20))
                break;
            break;

        case 20: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 21;
                time_cnt = 0;
            }
            break;

        case 21: /* 冲水 50.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 500, &status, 22))
                break;
            break;

        case 22: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 23;
                time_cnt = 0;
            }
            break;

        case 23: /* 中药 1.2s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 12, &status, 24))
                break;
            break;

        case 24: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 25;
                time_cnt = 0;
            }
            break;

        case 25: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 26))
                break;
            break;

        case 26: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 27;
                time_cnt = 0;
            }
            break;

        case 27: /* 中药 1.2s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 12, &status, 28))
                break;
            break;

        case 28: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 29;
                time_cnt = 0;
            }
            break;

        case 29: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 30))
                break;
            break;

        case 30: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 31;
                time_cnt = 0;
            }
            break;

        case 31: /* 中药 0.9s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 9, &status, 32))
                break;
            break;

        case 32: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 33;
                time_cnt = 0;
            }
            break;

        case 33: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 34))
                break;
            break;

        case 34: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 35;
                time_cnt = 0;
            }
            break;

        case 35: /* 中药 0.9s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 9, &status, 36))
                break;
            break;

        case 36: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 37;
                time_cnt = 0;
            }
            break;

        case 37: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 38))
                break;
            break;

        case 38: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 39;
                time_cnt = 0;
            }
            break;

        case 39: /* 中药 0.9s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 9, &status, 40))
                break;
            break;

        case 40: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 41;
                time_cnt = 0;
            }
            break;

        case 41: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 42))
                break;
            break;

        case 42: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 43;
                time_cnt = 0;
            }
            break;

        case 43: /* 中药 0.9s */
            if (time_cnt == 0)
            {
                mode4_zhongyao_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 9, &status, 44))
                break;
            break;

        case 44: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 45;
                time_cnt = 0;
            }
            break;

        case 45: /* 洗发水 1.5s */
            if (time_cnt == 0)
            {
                mode4_shampoo_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 15, &status, 46))
                break;
            break;

        case 46: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 47;
                time_cnt = 0;
            }
            break;

        case 47: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 48))
                break;
            break;

        case 48: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 49;
                time_cnt = 0;
            }
            break;

        case 49: /* 洗发水 1.2s */
            if (time_cnt == 0)
            {
                mode4_shampoo_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 12, &status, 50))
                break;
            break;

        case 50: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 51;
                time_cnt = 0;
            }
            break;

        case 51: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 52))
                break;
            break;

        case 52: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 53;
                time_cnt = 0;
            }
            break;

        case 53: /* 洗发水 1.2s */
            if (time_cnt == 0)
            {
                mode4_shampoo_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 12, &status, 54))
                break;
            break;

        case 54: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 55;
                time_cnt = 0;
            }
            break;

        case 55: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 56))
                break;
            break;

        case 56: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 57;
                time_cnt = 0;
            }
            break;

        case 57: /* 护发素 0.9s */
            if (time_cnt == 0)
            {
                mode4_conditioner_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 9, &status, 58))
                break;
            break;

        case 58: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 59;
                time_cnt = 0;
            }
            break;

        case 59: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 60))
                break;
            break;

        case 60: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 61;
                time_cnt = 0;
            }
            break;

        case 61: /* 护发素 0.6s */
            if (time_cnt == 0)
            {
                mode4_conditioner_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 6, &status, 62))
                break;
            break;

        case 62: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 63;
                time_cnt = 0;
            }
            break;

        case 63: /* 冲水 60.0s */
            if (time_cnt == 0)
            {
                mode4_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 600, &status, 64))
                break;
            break;

        case 64: /* 暂停 10.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 100)
            {
                status   = 65;
                time_cnt = 0;
            }
            break;

        case 65: /* 护发素 0.3s */
            if (time_cnt == 0)
            {
                mode4_conditioner_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 3, &status, 66))
                break;
            break;

        case 66: /* 暂停 5.0s */
            if (time_cnt == 0)
            {
                mode4_pause_enter();
            }
            if (++time_cnt >= 50)
            {
                status   = 67;
                time_cnt = 0;
            }
            break;

        case 67: /* 冲水 125.0s */
            if (time_cnt == 0)
            {
                mode4_final_rinse_enter();
                mode4_motor_run_enter();
            }
            if (mode4_active_step_tick(&time_cnt, 1250, &status, 68))
                break;
            break;

        case 68: /* 结束 */
            if (time_cnt == 0)
            {
                mode4_all_off_enter();
                gpio_set_level(MODE4_LIGHT_GPIO, 0);
                motor_finish();
            }
            if (++time_cnt >= 1)
            {
                ESP_LOGI(TAG, "mode4_zhongyao_v2 finished, runtime=%.1f s", runtime / 10.0f);
                return 0;
            }
            break;

        default:
            ESP_LOGE(TAG, "Unknown status %d", status);
            mode4_all_off_enter();
            mode4_motor_stop_enter();
            return -1;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        ++runtime;
    }
}
