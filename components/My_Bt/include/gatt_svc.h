/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#ifndef GATT_SVR_H
#define GATT_SVR_H

/* Includes */
/* NimBLE GATT APIs */
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

/* NimBLE GAP APIs */
#include "host/ble_gap.h"


/* 创建发送和接收队列 */
#define QUEUE_LENGTH 20
#define QUEUE_ITEM_SIZE 256 //数值不宜过大，否则造成栈溢出

/* BLE 队列数据结构 */
typedef struct
{
    uint8_t buf[QUEUE_ITEM_SIZE];
    size_t len;
} ble_data_t;

/* 公用函数声明 */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void gatt_svr_subscribe_cb(struct ble_gap_event *event);
int  gatt_svc_init(void);
void ble_queue_init(void);

/* 任务函数 */
void ble_send_task(void *param);
void ble_receive_task(void *param);


#endif // GATT_SVR_H
