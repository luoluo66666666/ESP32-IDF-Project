/**
 * Http_ota.h
 *
 * 简洁的 HTTP(S) OTA 任务接口（独立组件实现）
 * 使用方法：在 app 初始化后调用 `http_ota_start()` 启动 OTA 任务，
 * 使用 `http_ota_trigger()` 触发升级（可传临时 URL），最后可调用 `http_ota_stop()` 停止任务并释放资源。
 */

#ifndef HTTP_OTA_H
#define HTTP_OTA_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

/**
 * HTTP OTA 任务配置结构体
 */
typedef struct {
    char firmware_url[256];     // 默认固件 URL（可为空，但触发时需提供 URL）
    uint32_t task_stack_size;   // 任务栈大小（字节）
    UBaseType_t task_prio;      // 任务优先级
} http_ota_config_t;

/**
 * 启动 HTTP OTA 后台任务
 * @param config 非 NULL，指向配置结构；函数内部会拷贝其内容
 * @return 成功返回任务句柄（非 NULL），失败返回 NULL
 */
TaskHandle_t http_ota_start(const http_ota_config_t *config);

/**
 * 停止并销毁 HTTP OTA 任务，释放所有资源
 * @param task_handle 由 http_ota_start 返回的任务句柄
 */
void http_ota_stop(TaskHandle_t task_handle);

/**
 * 触发一次 HTTP OTA 升级
 * @param firmware_url 临时固件 URL，可为 NULL（使用默认 URL）
 * @return ESP_OK 成功；其它值为错误码
 */
esp_err_t http_ota_trigger(const char *firmware_url);

#endif // HTTP_OTA_H
