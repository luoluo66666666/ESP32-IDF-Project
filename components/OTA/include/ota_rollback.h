/**
 * @file ota_rollback.h
 * @brief OTA 新固件自检：稳定运行后 confirm，否则 Bootloader 自动回上一版
 */

#ifndef OTA_ROLLBACK_H
#define OTA_ROLLBACK_H

#include "esp_err.h"

/**
 * 上电早期调用：打印当前分区/OTA 状态（含 pending verify、回滚后启动）
 */
void ota_rollback_init(void);

/**
 * 各模块初始化完成后调用：延迟若干秒仍正常运行则 mark valid，取消回滚
 */
void ota_rollback_schedule_confirm(void);

#endif /* OTA_ROLLBACK_H */
