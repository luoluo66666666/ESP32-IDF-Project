#include "ctrl_protocol.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "Http_ota.h"
#include "modbus.h"
#include "mode_ctrl.h"
#include "rs485_water_valve.h"

extern EventGroupHandle_t event_ctrl_protocol;
extern int do_pin[26];

const char *TAG = "CTRL_PROTOCOL";
EventGroupHandle_t event_motor_ctrl;

#define STEP_DELAY_MS 50
#define PHASE_TIME_MS 5000

typedef struct {
    const char *cmd;
    int mode_index;
} mode_cmd_t;

typedef enum {
    POLE_RETRACTED = 0,
    POLE_EXTENDED = 1
} pole_state_t;

static const mode_cmd_t s_mode_commands[] = {
    {"CMD:MODE0", 0},
    {"CMD:MODE1", 1},
    {"CMD:MODE2", 2},
    {"CMD:MODE3", 3},
    {"CMD:MODE4", 4},
    {"CMD:MODE5_UPPER", 5},
    {"CMD:MODE5_LOWER", 6},
};

#define MODE_CMD_COUNT (sizeof(s_mode_commands) / sizeof(s_mode_commands[0]))

/* 处理模式查询命令 */
static bool handle_mode_query(const char *input, char *output, int maxlen)
{
    if (strncmp(input, "CMD:MODE_GET", strnlen("CMD:MODE_GET", maxlen)) != 0) {
        return false;
    }

    check_status();
    snprintf(output, maxlen, "CMD:MODE_GET,OK\r\n");
    return true;
}

/* 处理 DO 命令 */
static bool handle_do_command(const char *input, char *output, int maxlen)
{
    int do_index = -1;
    char action[8] = {0};

    if (strcasecmp(input, "all on") == 0) {
        for (int i = 0; i < (sizeof(do_pin) / sizeof(do_pin[0])); i++) {
            set_do_pin(i, 1);
        }
        snprintf(output, maxlen, "ALL,ON\r\n");
        return true;
    }

    if (strcasecmp(input, "all off") == 0) {
        for (int i = 0; i < (sizeof(do_pin) / sizeof(do_pin[0])); i++) {
            set_do_pin(i, 0);
        }
        snprintf(output, maxlen, "ALL,OFF\r\n");
        return true;
    }

    if (strncasecmp(input, "do", 2) != 0) {
        return false;
    }

    if (sscanf(input, "do%d %7s", &do_index, action) != 2) {
        return false;
    }

    if (do_index < 0 || do_index >= (sizeof(do_pin) / sizeof(do_pin[0]))) {
        return false;
    }

    if (strcasecmp(action, "on") == 0) {
        set_do_pin(do_index, 1);
        snprintf(output, maxlen, "do%d,ON\r\n", do_index);
        return true;
    }

    if (strcasecmp(action, "off") == 0) {
        set_do_pin(do_index, 0);
        snprintf(output, maxlen, "do%d,OFF\r\n", do_index);
        return true;
    }

    if (strcasecmp(action, "toggle") == 0) {
        set_do_pin(do_index, !get_do_pin(do_index));
        snprintf(output, maxlen, "do%d,TOGGLED\r\n", do_index);
        return true;
    }

    return false;
}

/* 处理模式切换命令 */
static bool handle_mode_switch(const char *input, char *output, int maxlen)
{
    for (size_t i = 0; i < MODE_CMD_COUNT; i++) {
        if (strncmp(input, s_mode_commands[i].cmd, strnlen(s_mode_commands[i].cmd, maxlen)) == 0) {
            if (set_mode(s_mode_commands[i].mode_index) >= 0) {
                snprintf(output, maxlen, "%s,OK\r\n", s_mode_commands[i].cmd);
            } else {
                snprintf(output, maxlen, "%s,ERROR\r\n", s_mode_commands[i].cmd);
            }
            return true;
        }
    }

    return false;
}

/* 处理水阀命令 */
static bool handle_water_command(const char *input, char *output, int maxlen)
{
    char action[16] = {0};
    int value = 0;

    if (strncasecmp(input, "water", 5) != 0) {
        return false;
    }

    RS485_init();

    if (sscanf(input, "water %15s %d", action, &value) < 1) {
        return false;
    }

    if (strcasecmp(action, "set") == 0) {
        rs485_write_register(0x01, 0x0005, value);
        snprintf(output, maxlen, "WATER,SET,%d\r\n", value);
        return true;
    }

    if (strcasecmp(action, "get") == 0) {
        rs485_read_register(0x01, 0x0000, 12);
        snprintf(output, maxlen, "WATER,READ_OK\r\n");
        return true;
    }

    return false;
}

/* 处理旧版温控测试命令 */
static bool handle_temp_legacy_command(const char *input, char *output, int maxlen)
{
    uint8_t temp_addr = 1;
    char action[16] = {0};
    int value = 0;
    int args;

    if (strncasecmp(input, "temp test_old", 13) != 0) {
        return false;
    }

    RS485_init();
    args = sscanf(input, "temp %15s %d", action, &value);
    if (args < 1) {
        snprintf(output, maxlen, "TEMP,ERR,BAD_FORMAT\r\n");
        return true;
    }

    if (strcasecmp(action, "test_old") == 0) {
        temp_test_sequence();
        temp_rs485_read_register(temp_addr, 0x0000, 2);
        vTaskDelay(pdMS_TO_TICKS(200));
        temp_rs485_read_register(temp_addr, 0x0001, 4);
        snprintf(output, maxlen, "TEMP,TEST_OLD,OK\r\n");
    } else {
        snprintf(output, maxlen, "TEMP,ERR,UNKNOWN_ACTION\r\n");
    }

    return true;
}

/* 处理按摩椅命令 */
static bool handle_massage_command(const char *input, char *output, int maxlen)
{
    char action[16] = {0};
    char param[16] = {0};
    esp_err_t err = ESP_ERR_INVALID_ARG;

    if (strncasecmp(input, "massage", 7) != 0) {
        return false;
    }

    if (sscanf(input, "massage %15s %15s", action, param) >= 1) {
        if (strcasecmp(action, "stop") == 0) {
            err = massage_chair_stop();
            snprintf(output, maxlen, (err == ESP_OK) ? "MASSAGE,STOP,OK\r\n" : "MASSAGE,STOP,ERR\r\n");
            return true;
        }

        if (strcasecmp(action, "test") == 0) {
            xTaskCreate(massage_chair_test_task, "massage_test_task", 4096, NULL, 8, NULL);
            snprintf(output, maxlen, "MASSAGE,TEST,OK\r\n");
            return true;
        }

        if (strcasecmp(action, "mode") == 0) {
            if (strcmp(param, "1") == 0) err = massage_chair_set_mode(MASSAGE_MODE_1);
            else if (strcmp(param, "2") == 0) err = massage_chair_set_mode(MASSAGE_MODE_2);
            else if (strcmp(param, "3") == 0) err = massage_chair_set_mode(MASSAGE_MODE_3);

            snprintf(output, maxlen, (err == ESP_OK) ? "MASSAGE,MODE,%s,OK\r\n" : "MASSAGE,MODE,ERR\r\n", param);
            return true;
        }

        if (strcasecmp(action, "strength") == 0) {
            if (strcasecmp(param, "low") == 0) err = massage_chair_set_strength_low();
            else if (strcasecmp(param, "medium") == 0) err = massage_chair_set_strength_medium();
            else if (strcasecmp(param, "high") == 0) err = massage_chair_set_strength_high();

            snprintf(output, maxlen, (err == ESP_OK) ? "MASSAGE,STRENGTH,%s,OK\r\n" : "MASSAGE,STRENGTH,ERR\r\n", param);
            return true;
        }
    }

    snprintf(output, maxlen, "MASSAGE,ERR,BAD_FORMAT\r\n");
    return true;
}

/* 处理变频器命令 */
static bool handle_inverter_command(const char *input, char *output, int maxlen)
{
    char action[16] = {0};
    float value = 0.0f;
    uint16_t raw = 0;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    int args;

    if (strncasecmp(input, "inverter", 8) != 0) {
        return false;
    }

    args = sscanf(input, "inverter %15s %f", action, &value);
    if (args >= 1) {
        if (strcasecmp(action, "start") == 0) {
            err = inverter_start();
            snprintf(output, maxlen, (err == ESP_OK) ? "INVERTER,START,OK\r\n" : "INVERTER,START,ERR\r\n");
            return true;
        }

        if (strcasecmp(action, "test") == 0) {
            xTaskCreate(inverter_test_task, "inverter_test_task", 4096, NULL, 8, NULL);
            snprintf(output, maxlen, "INVERTER,TEST,OK\r\n");
            return true;
        }

        if (strcasecmp(action, "stop") == 0) {
            err = inverter_stop();
            snprintf(output, maxlen, (err == ESP_OK) ? "INVERTER,STOP,OK\r\n" : "INVERTER,STOP,ERR\r\n");
            return true;
        }

        if (strcasecmp(action, "set") == 0 && args == 2) {
            err = inverter_set_pressure(value);
            if (err == ESP_OK) {
                snprintf(output, maxlen, "INVERTER,SET,%.1f,OK\r\n", value);
            } else {
                snprintf(output, maxlen, "INVERTER,SET,ERR\r\n");
            }
            return true;
        }

        if (strcasecmp(action, "get") == 0) {
            err = inverter_read_pressure_raw(&raw);
            if (err == ESP_OK) {
                snprintf(output, maxlen, "INVERTER,GET,%u\r\n", raw);
            } else {
                snprintf(output, maxlen, "INVERTER,GET,ERR\r\n");
            }
            return true;
        }
    }

    snprintf(output, maxlen, "INVERTER,ERR,BAD_FORMAT\r\n");
    return true;
}

/* 处理恒温器命令 */
static bool handle_temp_command(const char *input, char *output, int maxlen)
{
    char action[16] = {0};
    int value = 0;
    int args;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    uint16_t reg = 0;
    uint16_t regs[4] = {0};
    uint16_t regs_head[2] = {0};

    if (strncasecmp(input, "temp", 4) != 0) {
        return false;
    }

    args = sscanf(input, "temp %15s %d", action, &value);
    if (args >= 1) {
        if (strcasecmp(action, "set") == 0) {
            err = modbus_write_single_register(0x01, 0x0000, 0x00C0);
            if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
            if (err == ESP_OK) err = modbus_write_single_register(0x01, 0x0001, (uint16_t)value);
            if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
            if (err == ESP_OK) err = modbus_read_holding_registers(0x01, 0x0000, 2, regs_head);
            if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
            if (err == ESP_OK) err = temp_read_target_temperature(&reg);
            if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
            if (err == ESP_OK) err = modbus_read_holding_registers(0x01, 0x0001, 4, regs);
            if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

            if (err == ESP_OK) {
                snprintf(output, maxlen, "TEMP,SET,%d\r\n", value);
            } else {
                snprintf(output, maxlen, "TEMP,SET,ERR\r\n");
            }
            return true;
        }

        if (strcasecmp(action, "get") == 0) {
            err = modbus_read_holding_registers(0x01, 0x0000, 2, regs_head);
            if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
            if (err == ESP_OK) err = modbus_read_holding_registers(0x01, 0x0001, 4, regs);

            if (err == ESP_OK) {
                snprintf(output, maxlen, "TEMP,READ_OK\r\n");
            } else {
                snprintf(output, maxlen, "TEMP,READ_ERR\r\n");
            }
            return true;
        }

        if (strcasecmp(action, "water") == 0) {
            err = temp_read_water_temperature(&reg);
            if (err == ESP_OK) {
                snprintf(output, maxlen, "TEMP,WATER,%u\r\n", reg);
            } else {
                snprintf(output, maxlen, "TEMP,WATER,ERR\r\n");
            }
            return true;
        }

        if (strcasecmp(action, "flow") == 0) {
            err = temp_read_water_flow(&reg);
            if (err == ESP_OK) {
                snprintf(output, maxlen, "TEMP,FLOW,%u\r\n", reg);
            } else {
                snprintf(output, maxlen, "TEMP,FLOW,ERR\r\n");
            }
            return true;
        }

        if (strcasecmp(action, "all") == 0) {
            err = temp_read_water_temp_flow(regs);
            if (err == ESP_OK) {
                snprintf(output, maxlen, "TEMP,ALL,%u,%u,%u\r\n", regs[0], regs[1], regs[2]);
            } else {
                snprintf(output, maxlen, "TEMP,ALL,ERR\r\n");
            }
            return true;
        }

        if (strcasecmp(action, "test") == 0) {
            xTaskCreate(temp_test_task, "temp_test_task", 4096, NULL, 8, NULL);
            snprintf(output, maxlen, "TEMP,TEST,OK\r\n");
            return true;
        }
    }

    snprintf(output, maxlen, "TEMP,ERR,BAD_FORMAT\r\n");
    return true;
}

/* 处理 OTA 命令 */
static bool handle_ota_command(const char *input, char *output, int maxlen)
{
    char url[256] = {0};

    if (strncasecmp(input, "ota", 3) != 0) {
        return false;
    }

    if (sscanf(input, "ota %255s", url) == 1) {
        esp_err_t ret = http_ota_trigger(url);
        if (ret == ESP_OK) {
            snprintf(output, maxlen, "OTA,URL_TRIGGERED,OK\r\n");
            ESP_LOGI(TAG, "OTA with custom URL triggered successfully");
        } else {
            snprintf(output, maxlen, "OTA,URL_TRIGGERED,ERR\r\n");
            ESP_LOGE(TAG, "Failed to trigger OTA with custom URL: %s", esp_err_to_name(ret));
        }
        return true;
    }

    {
        esp_err_t ret = http_ota_trigger(NULL);
        if (ret == ESP_OK) {
            snprintf(output, maxlen, "OTA,DEFAULT,OK\r\n");
            ESP_LOGI(TAG, "OTA with default URL triggered successfully");
        } else {
            snprintf(output, maxlen, "OTA,DEFAULT,ERR\r\n");
            ESP_LOGE(TAG, "Failed to trigger OTA with default URL: %s", esp_err_to_name(ret));
        }
    }

    return true;
}

/* 撑杆电机控制任务 */
void Pole_motor_control_task(void *p)
{
    pole_state_t pole1_state = POLE_RETRACTED;
    pole_state_t pole2_state = POLE_RETRACTED;
    TickType_t step_delay = pdMS_TO_TICKS(STEP_DELAY_MS);
    TickType_t phase_time = pdMS_TO_TICKS(PHASE_TIME_MS);

    TURN_OFF(0);
    TURN_OFF(1);
    TURN_OFF(20);
    TURN_OFF(21);

    while (1) {
        xEventGroupWaitBits(event_motor_ctrl, Motor_RUN_BIT | Motor_Finsh_BIT,
                            pdFALSE, pdFALSE, portMAX_DELAY);

        if (xEventGroupGetBits(event_motor_ctrl) & Motor_Finsh_BIT) {
finsh_motor:
            TURN_OFF(0);
            TURN_ON(1);
            pole1_state = POLE_RETRACTED;
            TURN_OFF(20);
            TURN_ON(21);
            pole2_state = POLE_RETRACTED;
            vTaskDelay(pdMS_TO_TICKS(5000));
            TURN_OFF(21);
            TURN_OFF(1);

            xEventGroupClearBits(event_motor_ctrl,
                                 Motor_Finsh_BIT | Motor_RUN_BIT | Motor_STOP_BIT);
            ESP_LOGI(TAG, "Motor FINISH detected, all retracted");
            continue;
        }

        while (xEventGroupGetBits(event_motor_ctrl) & Motor_RUN_BIT) {
            if (pole1_state == POLE_RETRACTED && pole2_state == POLE_RETRACTED) {
                TURN_OFF(0);
                TURN_ON(1);
                pole1_state = POLE_EXTENDED;
                TURN_OFF(21);
                TURN_ON(20);
                pole2_state = POLE_RETRACTED;
            } else if (pole1_state == POLE_EXTENDED && pole2_state == POLE_RETRACTED) {
                TURN_OFF(1);
                TURN_ON(0);
                pole1_state = POLE_RETRACTED;
                TURN_OFF(20);
                TURN_ON(21);
                pole2_state = POLE_EXTENDED;
            } else if (pole1_state == POLE_RETRACTED && pole2_state == POLE_EXTENDED) {
                TURN_OFF(0);
                TURN_ON(1);
                pole1_state = POLE_EXTENDED;
                TURN_OFF(21);
                TURN_ON(20);
                pole2_state = POLE_RETRACTED;
            } else {
                TURN_OFF(0);
                TURN_OFF(1);
                TURN_OFF(20);
                TURN_OFF(21);
            }

            {
                TickType_t start_tick = xTaskGetTickCount();
                while (xTaskGetTickCount() - start_tick < phase_time) {
                    EventBits_t bits = xEventGroupGetBits(event_motor_ctrl);
                    if (bits & Motor_STOP_BIT) goto stop_motor;
                    if (bits & Motor_Finsh_BIT) goto finsh_motor;
                    vTaskDelay(step_delay);
                }
            }
        }

stop_motor:
        TURN_OFF(0);
        TURN_OFF(1);
        TURN_OFF(20);
        TURN_OFF(21);
        xEventGroupClearBits(event_motor_ctrl, Motor_STOP_BIT | Motor_RUN_BIT);
        ESP_LOGI(TAG, "Motor STOP detected, hold state P1=%d, P2=%d", pole1_state, pole2_state);
    }
}

/* 获取故障状态 */
bool get_fault_status(void)
{
    if (event_ctrl_protocol == NULL) {
        return false;
    }

    return (xEventGroupGetBits(event_ctrl_protocol) & FAULT_BIT) != 0;
}

/* 获取运行状态 */
bool get_run_status(void)
{
    if (event_ctrl_protocol == NULL) {
        return false;
    }

    return (xEventGroupGetBits(event_ctrl_protocol) & RUN_BIT) != 0;
}

/* 获取当前模式 */
int get_mode_status(void)
{
    EventBits_t event_bits;

    if (event_ctrl_protocol == NULL) {
        return -1;
    }

    event_bits = xEventGroupGetBits(event_ctrl_protocol);
    if (event_bits & Mode0_BIT) return 0;
    if (event_bits & Mode1_BIT) return 1;
    if (event_bits & Mode2_BIT) return 2;
    if (event_bits & Mode3_BIT) return 3;
    if (event_bits & Mode4_BIT) return 4;
    if (event_bits & Mode5_UPPER_BIT) return 5;
    if (event_bits & Mode5_LOWER_BIT) return 6;
    return -1;
}

/* 检查系统状态 */
int check_status(void)
{
    int mode;

    if (get_fault_status()) {
        ESP_LOGE(TAG, "System is in fault state");
        return -1;
    }

    mode = get_mode_status();
    if (mode >= 0) {
        ESP_LOGI(TAG, "System is already running in mode: %d", mode);
        return -1;
    }

    if (get_run_status()) {
        ESP_LOGI(TAG, "System is running, but no mode set.");
    } else {
        ESP_LOGI(TAG, "System is not running.");
    }

    return 0;
}

/* 设置模式 */
int set_mode(int mode)
{
    if (event_ctrl_protocol == NULL) {
        ESP_LOGE(TAG, "Event group not initialized");
        return -1;
    }

    if (get_run_status() || get_fault_status()) {
        ESP_LOGE(TAG, "Cannot change mode while running or in fault state");
        return -1;
    }

    if (check_status() < 0) {
        ESP_LOGE(TAG, "System is in fault state or already running in a mode");
        return -1;
    }

    xEventGroupClearBits(event_ctrl_protocol,
                         Mode0_BIT | Mode1_BIT | Mode2_BIT | Mode3_BIT |
                             Mode4_BIT | Mode5_UPPER_BIT | Mode5_LOWER_BIT);

    switch (mode) {
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
        return -1;
    }

    return mode;
}

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

/* 初始化控制协议 */
void ctrl_protocol_init(void)
{
    event_ctrl_protocol = xEventGroupCreate();
    event_motor_ctrl = xEventGroupCreate();

    if (event_ctrl_protocol == NULL || event_motor_ctrl == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }

    xEventGroupClearBits(event_ctrl_protocol,
                         Mode0_BIT | Mode1_BIT | Mode2_BIT | Mode3_BIT |
                             Mode4_BIT | Mode5_UPPER_BIT | Mode5_LOWER_BIT |
                             RUN_BIT | FAULT_BIT);
    xEventGroupClearBits(event_motor_ctrl,
                         Motor_RUN_BIT | Motor_STOP_BIT | Motor_GET_BIT | Motor_Finsh_BIT);
}

/* 控制协议入口 */
void ctrl_protocol(char *input, char *output, int maxlen)
{
    ESP_LOGI(TAG, "Received command: %s", input);

    if (handle_mode_query(input, output, maxlen)) return;
    if (handle_do_command(input, output, maxlen)) return;
    if (handle_mode_switch(input, output, maxlen)) return;
    if (handle_water_command(input, output, maxlen)) return;
    if (handle_temp_legacy_command(input, output, maxlen)) return;
    if (handle_massage_command(input, output, maxlen)) return;
    if (handle_inverter_command(input, output, maxlen)) return;
    if (handle_temp_command(input, output, maxlen)) return;
    if (handle_ota_command(input, output, maxlen)) return;

    ESP_LOGE(TAG, "Invalid command");
    snprintf(output, maxlen, "CMD:ERR\r\n");
}
