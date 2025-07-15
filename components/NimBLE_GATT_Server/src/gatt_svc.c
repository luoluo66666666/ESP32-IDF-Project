/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "gatt_svc.h"
#include "common.h"
#include "heart_rate.h"
#include "uart_module.h"

/* 创建发送和接收队列 */
#define QUEUE_LENGTH 10
#define QUEUE_ITEM_SIZE 256

static QueueHandle_t ble_tx_queue = NULL;
static QueueHandle_t ble_rx_queue = NULL;

typedef struct {
    uint8_t buf[QUEUE_ITEM_SIZE];
    size_t len;
} ble_data_t;

/* 初始化队列 */
void ble_queue_init(void) {
    ble_tx_queue = xQueueCreate(QUEUE_LENGTH, sizeof(ble_data_t));
    ble_rx_queue = xQueueCreate(QUEUE_LENGTH, sizeof(ble_data_t));
}

/* Private function declarations */
static int heart_rate_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Private variables */
/* Heart rate service */
static const ble_uuid16_t heart_rate_svc_uuid = BLE_UUID16_INIT(0x180D);

static uint8_t heart_rate_chr_val[2] = {0};
static uint16_t heart_rate_chr_val_handle;
static const ble_uuid16_t heart_rate_chr_uuid = BLE_UUID16_INIT(0x2A37);

static uint16_t heart_rate_chr_conn_handle = 0;
static bool heart_rate_chr_conn_handle_inited = false;
static bool heart_rate_ind_status = false;

/* Automation IO service */
static const ble_uuid16_t auto_io_svc_uuid = BLE_UUID16_INIT(0x1815);
static uint16_t led_chr_val_handle;
static const ble_uuid128_t led_chr_uuid =
    BLE_UUID128_INIT(0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef,
                     0x12, 0x12, 0x25, 0x15, 0x00, 0x00);

/* GATT services table */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    /* Heart rate service */
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &heart_rate_svc_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){
             {/* Heart rate characteristic */
              .uuid = &heart_rate_chr_uuid.u,
              .access_cb = heart_rate_chr_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &heart_rate_chr_val_handle},
             {
                 0, /* No more characteristics in this service. */
             }}},

    /* Automation IO service */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &auto_io_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){/* LED characteristic */
                                        {.uuid = &led_chr_uuid.u,
                                         .access_cb = led_chr_access,
                                         .flags = BLE_GATT_CHR_F_WRITE,
                                         .val_handle = &led_chr_val_handle},
                                        {0}},
    },

    {
        0, /* No more services. */
    },
};

/* Private functions */
static int heart_rate_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Local variables */
    int rc;

    /* Handle access events */
    /* Note: Heart rate characteristic is read only */
    switch (ctxt->op) {

    /* Read characteristic event */
    case BLE_GATT_ACCESS_OP_READ_CHR:
        /* Verify connection handle */
        if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ESP_LOGI(TAG, "characteristic read; conn_handle=%d attr_handle=%d",
                     conn_handle, attr_handle);
        } else {
            ESP_LOGI(TAG, "characteristic read by nimble stack; attr_handle=%d",
                     attr_handle);
        }

        /* Verify attribute handle */
        if (attr_handle == heart_rate_chr_val_handle) {
            /* Update access buffer value */
            heart_rate_chr_val[1] = get_heart_rate();
            rc = os_mbuf_append(ctxt->om, &heart_rate_chr_val,
                                sizeof(heart_rate_chr_val));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        goto error;

    /* Unknown event */
    default:
        goto error;
    }

error:
    ESP_LOGE(
        TAG,
        "unexpected access operation to heart rate characteristic, opcode: %d",
        ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

// static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
//                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
//     /* Local variables */
//     int rc;

//     /* Handle access events */
//     /* Note: LED characteristic is write only */
//     switch (ctxt->op) {

//     /* Write characteristic event */
//     case BLE_GATT_ACCESS_OP_WRITE_CHR:
//         /* Verify connection handle */
//         if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
//             ESP_LOGI(TAG, "characteristic write; conn_handle=%d attr_handle=%d",
//                      conn_handle, attr_handle);
//         } else {
//             ESP_LOGI(TAG,
//                      "characteristic write by nimble stack; attr_handle=%d",
//                      attr_handle);
//         }

//         /* Verify attribute handle */
//         if (attr_handle == led_chr_val_handle) {
//             /* Verify access buffer length */
//             if (ctxt->om->om_len == 1) {
//                 /* Turn the LED on or off according to the operation bit */
//                 if (ctxt->om->om_data[0]) {
//                     // led_on();
//                     ESP_LOGI(TAG, "led turned on!");
//                 } else {
//                     // led_off();
//                     ESP_LOGI(TAG, "led turned off!");
//                 }
//             } else {
//                 goto error;
//             }
//             return rc;
//         }
//         goto error;

//     /* Unknown event */
//     default:
//         goto error;
//     }

// error:
//     ESP_LOGE(TAG,
//              "unexpected access operation to led characteristic, opcode: %d",
//              ctxt->op);
//     return BLE_ATT_ERR_UNLIKELY;
// }


static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    /* Local variables */
    int rc;

    /* Handle access events */
    /* Note: LED characteristic is write only */
    switch (ctxt->op) {

    /* Write characteristic event */
    case BLE_GATT_ACCESS_OP_WRITE_CHR: 
    {
        int len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > QUEUE_ITEM_SIZE) len = QUEUE_ITEM_SIZE;

        ble_data_t buf = {0};
        os_mbuf_copydata(ctxt->om, 0, len, buf.buf);
        buf.len = len;

        if (ble_rx_queue != NULL) {
            if (xQueueSendFromISR(ble_rx_queue, &buf, NULL) != pdTRUE) {
                ESP_LOGW(TAG, "RX queue full, data lost");
            } else {
                ESP_LOGI(TAG, "Data queued in RX queue: %.*s", len, buf.buf);
            }
        }

    return 0;
    }
        /* Unknown event */
    default:
        goto error;
    }

error:
    ESP_LOGE(TAG,
             "unexpected access operation to led characteristic, opcode: %d",
             ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

/* Public functions */
void send_heart_rate_indication(void) {
    if (heart_rate_ind_status && heart_rate_chr_conn_handle_inited) {
        ble_gatts_indicate(heart_rate_chr_conn_handle,
                           heart_rate_chr_val_handle);
        ESP_LOGI(TAG, "heart rate indication sent!");
    }
}

/*
 *  Handle GATT attribute register events
 *      - Service register event
 *      - Characteristic register event
 *      - Descriptor register event
 */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    /* Local variables */
    char buf[BLE_UUID_STR_LEN];

    /* Handle GATT attributes register events */
    switch (ctxt->op) {

    /* Service register event */
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;

    /* Characteristic register event */
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG,
                 "registering characteristic %s with "
                 "def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;

    /* Descriptor register event */
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGD(TAG, "registering descriptor %s with handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;

    /* Unknown event */
    default:
        assert(0);
        break;
    }
}

/*
 *  GATT server subscribe event callback
 *      1. Update heart rate subscription status
 */

// void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
//     /* Check connection handle */
//     if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
//         ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
//                  event->subscribe.conn_handle, event->subscribe.attr_handle);
//     } else {
//         ESP_LOGI(TAG, "subscribe by nimble stack; attr_handle=%d",
//                  event->subscribe.attr_handle);
//     }

//     /* Check attribute handle */
//     if (event->subscribe.attr_handle == heart_rate_chr_val_handle) {
//         /* Update heart rate subscription status */
//         heart_rate_chr_conn_handle = event->subscribe.conn_handle;
//         heart_rate_chr_conn_handle_inited = true;
//         heart_rate_ind_status = event->subscribe.cur_indicate;
//     }
// }


// 订阅回调中，更新连接状态和订阅状态（cur_notify 和 cur_indicate）
void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
    } else {
        ESP_LOGI(TAG, "subscribe by nimble stack; attr_handle=%d",
                 event->subscribe.attr_handle);
    }

    if (event->subscribe.attr_handle == heart_rate_chr_val_handle) {
        heart_rate_chr_conn_handle = event->subscribe.conn_handle;
        heart_rate_chr_conn_handle_inited = true;
        // 这里合并通知和指示状态判断
        heart_rate_ind_status = event->subscribe.cur_notify || event->subscribe.cur_indicate;

        ESP_LOGI(TAG, "Notify/Indicate status updated: %d", heart_rate_ind_status);
    }
}



/*
 *  GATT server initialization
 *      1. Initialize GATT service
 *      2. Update NimBLE host GATT services counter
 *      3. Add GATT services to server
 */
int gatt_svc_init(void) {
    /* Local variables */
    int rc;

    /* 1. GATT service initialization */
    ble_svc_gatt_init();

    /* 2. Update GATT services counter */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    /* 3. Add GATT services */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}


// 发送任务：从队列取数据发送通知
void ble_send_task(void *param) {
    ble_data_t data;
    while (1) {
        if (heart_rate_chr_conn_handle_inited && heart_rate_ind_status) {
            if (xQueueReceive(ble_tx_queue, &data, portMAX_DELAY) == pdTRUE) {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(data.buf, data.len);
                if (om == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate mbuf");
                    continue;
                }
                int rc = ble_gatts_notify_custom(heart_rate_chr_conn_handle, heart_rate_chr_val_handle, om);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Notify send failed, rc=%d", rc);
                } else {
                    ESP_LOGI(TAG, "Notify sent: %.*s", (int)data.len, data.buf);
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}



void ble_receive_task(void *param) {
    ble_data_t data;
    while (1) {
        if (xQueueReceive(ble_rx_queue, &data, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Received data processed: %.*s", (int)data.len, data.buf);
            /* TODO: 自己处理收到的数据 */
        }
    }
    vTaskDelete(NULL);
}


// 外部调用函数：向发送队列放入数据

int ble_send_data(const char *data) {
    if (ble_tx_queue == NULL) return -1;

    size_t len = strlen(data);
    if (len == 0 || len >= QUEUE_ITEM_SIZE) return -2;

    ble_data_t send_data = {0};
    memcpy(send_data.buf, data, len);
    send_data.len = len;

    if (xQueueSend(ble_tx_queue, &send_data, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        ESP_LOGI(TAG, "Data queued for send: %s", data);
        return 0;
    } else {
        ESP_LOGW(TAG, "Send queue full, send failed");
        return -3;
    }
}


void uart_send_task(void *param) {
    uint8_t data[QUEUE_ITEM_SIZE];
    while (1) {
        if (xQueueReceive(ble_rx_queue, data, portMAX_DELAY) == pdTRUE) {
            // 发送数据到串口
            uart_write_bytes(UART_NUM_1, (const char *)data, strlen((char *)data));
            ESP_LOGI(TAG, "Sent to UART: %s", data);
        }
    }
}
