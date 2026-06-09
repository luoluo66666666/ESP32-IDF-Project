#include "temp.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_master.h"

/*
 * 恒温器（Modbus 从站 0x01）水温读写与门控缓存
 * - 485 失败：modbus 层推 RS485,ERR
 * - 读通但未达标：temp_push_not_ready_once 推 TEMP,ERR,NOT_READY,...
 * - 不在后台轮询，洗涤/ temp 命令用时才读
 */

#define TAG "TEMP"
#define TEMP_SLAVE_ADDR 0x01          /* 恒温器 Modbus 从站地址 */
#define TEMP_ENABLE_REG 0x0000        /* 使能寄存器 */
#define TEMP_SET_REG 0x0001           /* 目标温度写入寄存器 */
#define TEMP_ENABLE_VALUE 0x00C0      /* 使能写入值 */
#define TEMP_TARGET_READ_REG 0x0001   /* 目标温度读回寄存器 */
#define TEMP_WATER_TEMP_REG 0x0002      /* 当前出水温度寄存器 */
#define TEMP_WATER_FLOW_REG 0x0003      /* 出水流量寄存器 */
#define TEMP_INIT_RETRY_COUNT 3         /* 上电默认目标温度写入重试次数 */

/* 读一次 Modbus，填充水温/目标温/允许上下限（供门控与 temp water 命令使用） */
esp_err_t temp_read_water_status(temp_water_status_t *status)
{
    uint16_t target = TEMP_DEFAULT_TARGET_C;

    if (status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    status->water_c  = 0;
    status->target_c = TEMP_DEFAULT_TARGET_C;
    status->min_c    = 0;
    status->max_c    = 0;

    if (temp_read_water_temperature(&status->water_c) != ESP_OK)
    {
        return ESP_FAIL;
    }

    if (temp_read_target_temperature(&target) != ESP_OK || target == 0)
    {
        target = TEMP_DEFAULT_TARGET_C;
    }

    status->target_c = target;
    status->min_c    = (target > TEMP_READY_TOLERANCE_C) ? (uint16_t)(target - TEMP_READY_TOLERANCE_C) : 0;
    status->max_c    = (uint16_t)(target + TEMP_READY_TOLERANCE_C);
    return ESP_OK;
}

/* 判断当前水温是否在 [min_c, max_c] 内 */
bool temp_is_water_ready(const temp_water_status_t *status)
{
    if (status == NULL)
    {
        return false;
    }

    return status->water_c >= status->min_c && status->water_c <= status->max_c;
}

/* 格式化为 TCP 推送行：TEMP,ERR,NOT_READY,CUR=..,TARGET=..,MIN=..,MAX=.. */
int temp_format_not_ready_line(char *buf, size_t buf_len, const temp_water_status_t *status)
{
    if (buf == NULL || buf_len == 0 || status == NULL)
    {
        return -1;
    }

    return snprintf(buf,
                    buf_len,
                    "TEMP,ERR,NOT_READY,CUR=%u,TARGET=%u,MIN=%u,MAX=%u",
                    status->water_c,
                    status->target_c,
                    status->min_c,
                    status->max_c);
}

/* 格式化为 TCP 推送行：TEMP,ERR,READ_FAIL（保留接口，485 失败现由 modbus 推 RS485,ERR） */
int temp_format_read_fail_line(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0)
    {
        return -1;
    }

    return snprintf(buf, buf_len, "TEMP,ERR,READ_FAIL");
}

static temp_event_push_fn s_temp_push_fn = NULL;   /* 云端 TCP 推送回调 */
static volatile bool s_temp_read_ok      = false;  /* 最近一次 485 读是否成功 */
static volatile bool s_temp_water_ready  = false;  /* 最近一次读通且温度在范围内 */
static temp_water_status_t s_temp_cached = {0};    /* 最近一次成功采样的水温状态 */

/* 注册 TCP 推送回调（与 mode_ctrl / modbus 共用 wifi_module_tcp_push_line） */
void temp_set_event_push_cb(temp_event_push_fn fn)
{
    s_temp_push_fn = fn;
}

/* 内部：推送一行到已注册的 TCP 回调 */
static void temp_push_event_line(const char *line)
{
    if (line == NULL || line[0] == '\0')
    {
        return;
    }

    if (s_temp_push_fn != NULL)
    {
        s_temp_push_fn(line);
    }
    else
    {
        ESP_LOGW(TAG, "event (no push cb): %s", line);
    }
}

/* 洗涤门控用：读成功且水温在目标±容差内才返回 true */
bool temp_get_water_ready(void)
{
    return s_temp_read_ok && s_temp_water_ready;
}

/* 取监测缓存（读失败时返回 ESP_FAIL） */
esp_err_t temp_get_cached_status(temp_water_status_t *status)
{
    if (status == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_temp_read_ok)
    {
        return ESP_FAIL;
    }

    *status = s_temp_cached;
    return ESP_OK;
}

/* 主动读 485 并更新 s_temp_* 缓存；失败时 modbus 层会推 RS485,ERR */
esp_err_t temp_refresh_cache(void)
{
    temp_water_status_t st = {0};
    bool read_ok           = (temp_read_water_status(&st) == ESP_OK);
    bool ready             = read_ok && temp_is_water_ready(&st);

    s_temp_read_ok     = read_ok;
    s_temp_water_ready = ready;
    if (read_ok)
    {
        s_temp_cached = st;
        return ESP_OK;
    }

    return ESP_FAIL;
}

/** 水温未达标时推送一次（通信失败由 modbus 层 RS485,ERR 负责） */
void temp_push_not_ready_once(void)
{
    char line[80] = {0};

    if (!s_temp_read_ok || s_temp_water_ready)
    {
        return;
    }

    temp_format_not_ready_line(line, sizeof(line), &s_temp_cached);
    temp_push_event_line(line);
    ESP_LOGW(TAG, "%s", line);
}

/** 占位任务：不在后台轮询 485，用时由 temp_refresh_cache / 命令触发 */
void temp_monitor_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "temp monitor idle (485 read on demand only)");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* 设置恒温器目标温度 */
esp_err_t temp_set_target_temperature(uint16_t temperature)
{
    esp_err_t err;
    uint16_t reg = 0;
    uint16_t regs[4] = {0};
    uint16_t regs_head[2] = {0};

    err = modbus_write_single_register(TEMP_SLAVE_ADDR, TEMP_ENABLE_REG, TEMP_ENABLE_VALUE);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = modbus_write_single_register(TEMP_SLAVE_ADDR, TEMP_SET_REG, temperature);
    }
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_ENABLE_REG, 2, regs_head);
    }
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = temp_read_target_temperature(&reg);
    }
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_SET_REG, 4, regs);
    }
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    return err;
}

/* 读取恒温器状态 */
esp_err_t temp_read_status(uint16_t head[2], uint16_t regs[4])
{
    esp_err_t err;

    if (head == NULL || regs == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    err = modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_ENABLE_REG, 2, head);
    if (err == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));

    if (err == ESP_OK) {
        err = modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_SET_REG, 4, regs);
    }

    return err;
}

/* 读取当前设定温度 */
esp_err_t temp_read_target_temperature(uint16_t *temperature)
{
    return modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_TARGET_READ_REG, 1, temperature);
}

/* 读取当前出水温度 */
esp_err_t temp_read_water_temperature(uint16_t *temperature)
{
    return modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_WATER_TEMP_REG, 1, temperature);
}

/* 读取当前出水流量 */
esp_err_t temp_read_water_flow(uint16_t *flow)
{
    return modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_WATER_FLOW_REG, 1, flow);
}

/* 读取当前出水温度和流量 */
esp_err_t temp_read_water_temp_flow(uint16_t regs[3])
{
    return modbus_read_holding_registers(TEMP_SLAVE_ADDR, TEMP_WATER_TEMP_REG, 3, regs);
}

/* 上电默认目标温度写入（main 调用；失败仅打日志，不推 TCP） */
esp_err_t temp_init_default_target(void)
{
    esp_err_t err = ESP_FAIL;
    uint16_t target = 0;

    vTaskDelay(pdMS_TO_TICKS(500));

    for (int i = 0; i < TEMP_INIT_RETRY_COUNT; i++) {
        err = temp_set_target_temperature(TEMP_DEFAULT_TARGET_C);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "default target set failed, retry=%d err=%s",
                     i + 1, esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
        err = temp_read_target_temperature(&target);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "default target initialized: set=%u readback=%u",
                     TEMP_DEFAULT_TARGET_C, target);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "default target readback failed, retry=%d err=%s",
                 i + 1, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    ESP_LOGE(TAG, "default target init failed after retries");
    return err;
}

/* 恒温器测试任务 */
void temp_test_task(void *arg)
{
    uint16_t target = 0;
    uint16_t water_temp = 0;
    uint16_t flow = 0;

    while (1) {
        esp_err_t err = temp_set_target_temperature(39);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "set target temperature 39 ok");
        } else {
            ESP_LOGE(TAG, "set target temperature failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));

        err = temp_read_target_temperature(&target);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "target temperature = %u", target);
        } else {
            ESP_LOGE(TAG, "read target temperature failed: %s", esp_err_to_name(err));
        }

        err = temp_read_water_temperature(&water_temp);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "water temperature = %u", water_temp);
        } else {
            ESP_LOGE(TAG, "read water temperature failed: %s", esp_err_to_name(err));
        }

        err = temp_read_water_flow(&flow);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "water flow = %u", flow);
        } else {
            ESP_LOGE(TAG, "read water flow failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
