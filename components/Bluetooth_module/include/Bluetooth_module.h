// #pragma once
// void bt_module_start(void);
/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#ifndef ___BLUETOOTH_MODULE_H__
#define ___BLUETOOTH_MODULE_H__

/* Includes */
/* STD APIs */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ESP APIs */
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

/* FreeRTOS APIs */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* NimBLE stack APIs */
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

/* Defines */
#define DEVICE_NAME "NimBLE_CONN"

#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

/* Defines */
#define TAG "NimBLE_Connection"
#define DEVICE_NAME "NimBLE_CONN"
#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

/* Public function declarations */
void adv_init(void);
int gap_init(void);
void ble_host_task(void *param);

int Bluetooth_startup_init(void);
void main_ble_example_task(void *param);
void bluetooth_init(void);

#endif // GAP_SVC_H


