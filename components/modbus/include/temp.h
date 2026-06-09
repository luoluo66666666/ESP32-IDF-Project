#ifndef __TEMP_H__
#define __TEMP_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*-----------------------------------------------------------
 * 水温监测 — 后台任务 + 只读缓存
 *
 * 485 通信失败由 modbus 层推送 RS485,ERR（每次收发失败各推一次）。
 * 用时调用 temp_refresh_cache() 读 Modbus；未达标可 temp_push_not_ready_once()。
 * 洗涤模式读 temp_get_water_ready() 缓存；未达标时阻塞不推进。
 *
 * 典型用法：
 *   if (temp_get_water_ready()) { ... }
 *----------------------------------------------------------*/
#define TEMP_DEFAULT_TARGET_C    39u
#define TEMP_READY_TOLERANCE_C   5u
#define TEMP_MONITOR_POLL_MS     1000u

/** 一次水温采样结果（寄存器值为整数摄氏度） */
typedef struct
{
    uint16_t water_c;   /**< 当前出水温度 */
    uint16_t target_c;  /**< 目标温度 */
    uint16_t min_c;     /**< 允许下限 target - tolerance */
    uint16_t max_c;     /**< 允许上限 target + tolerance */
} temp_water_status_t;

typedef void (*temp_event_push_fn)(const char *line);

/** 注册 TCP 推送回调（与 mode_ctrl_push_event 相同实现即可） */
void temp_set_event_push_cb(temp_event_push_fn fn);

/** 水温监测任务入口（main 里 xTaskCreate） */
void temp_monitor_task(void *param);

/** 监测任务缓存：水温是否就绪 */
bool temp_get_water_ready(void);

/** 监测任务缓存：最近一次成功采样的状态 */
esp_err_t temp_get_cached_status(temp_water_status_t *status);

/** 同步读 Modbus 并更新缓存（失败时 modbus 层推 RS485,ERR） */
esp_err_t temp_refresh_cache(void);

/** 通信已成功但水温未达标时推送 TEMP,ERR,NOT_READY,... */
void temp_push_not_ready_once(void);

/** 同步读 Modbus（仅供监测任务或调试命令） */
esp_err_t temp_read_water_status(temp_water_status_t *status);

/** 当前水温是否在 [min_c, max_c] 内 */
bool temp_is_water_ready(const temp_water_status_t *status);

/**
 * 格式化为 TCP 推送行：
 * TEMP,ERR,NOT_READY,CUR=35,TARGET=39,MIN=34,MAX=44
 */
int temp_format_not_ready_line(char *buf, size_t buf_len, const temp_water_status_t *status);

/** 读取失败：TEMP,ERR,READ_FAIL */
int temp_format_read_fail_line(char *buf, size_t buf_len);

/* 按旧流程设置恒温器目标温度 */
esp_err_t temp_set_target_temperature(uint16_t temperature);

/* 按旧流程读取恒温器状态 */
esp_err_t temp_read_status(uint16_t head[2], uint16_t regs[4]);

/* 读取当前设定温度 */
esp_err_t temp_read_target_temperature(uint16_t *temperature);

/* 读取当前出水温度 */
esp_err_t temp_read_water_temperature(uint16_t *temperature);

/* 读取当前出水流量 */
esp_err_t temp_read_water_flow(uint16_t *flow);

/* 读取当前出水温度和流量 */
esp_err_t temp_read_water_temp_flow(uint16_t regs[3]);

/* 初始化恒温器默认目标温度 */
esp_err_t temp_init_default_target(void);

/* 恒温器测试任务 */
void temp_test_task(void *arg);

#endif
