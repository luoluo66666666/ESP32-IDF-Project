/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "gatt_svc.h"
#include "common.h"
#include "heart_rate.h"
// #include "uart_module.h"

#include "ctrl_protocol.h" // Include the ctrl_protocol header for ctrl_protocol functions

char response[QUEUE_ITEM_SIZE];

/* --------------------------- 定义是否启用 BLE 加密访问 --------------------------- */
#define BLE_ENCRYPTION_REQUIRED 0 // 1：启用加密访问  0：不要求加密

QueueHandle_t ble_tx_queue = NULL;
QueueHandle_t ble_rx_queue = NULL;

/* 初始化队列 */
void ble_queue_init(void)
{
    if (ble_tx_queue == NULL)
    {
        ble_tx_queue = xQueueCreate(QUEUE_LENGTH, sizeof(ble_data_t));
        if (ble_tx_queue == NULL)
        {
            ESP_LOGE(TAG, "Failed to create ble_tx_queue");
            abort(); // 队列创建失败，直接终止程序
        }
    }

    if (ble_rx_queue == NULL)
    {
        ble_rx_queue = xQueueCreate(QUEUE_LENGTH, sizeof(ble_data_t));
        if (ble_rx_queue == NULL)
        {
            ESP_LOGE(TAG, "Failed to create ble_rx_queue");
            abort(); // 队列创建失败，直接终止程序
        }
    }

    ESP_LOGI(TAG, "BLE queues initialized successfully");
}

/* Private function declarations */
/* GATT 特征 (Characteristic) 的读写访问回调函数 */
static int data_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Private variables */
uint16_t custom_chr_conn_handle = 0;
bool custom_notify_enabled = false;


/*-------------------Private Define-----------------------*/
// 自定义服务 UUID（128位）
static const ble_uuid128_t my_custom_svc_uuid =
    BLE_UUID128_INIT(0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
                     0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0);

// 自定义特征 UUID（128位）
static const ble_uuid128_t my_custom_chr_uuid =
    BLE_UUID128_INIT(0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89,
                     0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89);

// 特征值句柄
static uint16_t my_custom_chr_val_handle;

static uint16_t g_conn_handle = 0;  // 保存连接句柄
static bool notify_enabled = false; // 客户端是否已启用 notify

/* ---------- 加密的设置 ---------- */
#if BLE_ENCRYPTION_REQUIRED
#define MY_CUSTOM_FLAGS (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY | \
                         BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC)
#else
#define MY_CUSTOM_FLAGS (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY)
#endif

/*******************************************************************************
****@brief: Nimble GATT(通用属性规范)服务定义表
****@author: Luo
****@date: 2025-08-27 08:15:02
********************************************************************************/
/* GATT services table */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {

    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,     // 定义一个 "主服务" (Primary Service)
        .uuid = &my_custom_svc_uuid.u,         // 服务的 UUID，这里是自定义的 UUID
        .characteristics =
            (struct ble_gatt_chr_def[]){       // 服务包含的特征值数组（以 {0} 结束）

                {   // 定义一个特征值 (Characteristic)
                    .uuid = &my_custom_chr_uuid.u,   // 特征值的 UUID，自定义
                    .access_cb = data_access,        // 读/写/通知时的回调函数
                    .flags = MY_CUSTOM_FLAGS,        // 特征值的属性标志（读、写、通知等）
                    .val_handle = &my_custom_chr_val_handle // 保存特征值的句柄（后续 notify 用到）
                },

                {0} // 数组结束标记，必须有
            },
    },

    {
        0, // 表示没有更多的服务，服务表以 {0} 结束
    }
};


/* Private functions */
/*
 *  Handle GATT attribute register events
 *      - Service register event
 *      - Characteristic register event
 *      - Descriptor register event
 */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    /* Local variables */
    char buf[BLE_UUID_STR_LEN];

    /* Handle GATT attributes register events */
    switch (ctxt->op)
    {

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

// 订阅回调中，更新连接状态和订阅状态（cur_notify 和 cur_indicate）
void gatt_svr_subscribe_cb(struct ble_gap_event *event)
{
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
    }
    else
    {
        ESP_LOGI(TAG, "subscribe by nimble stack; attr_handle=%d",
                 event->subscribe.attr_handle);
    }

    // 只处理自定义特征的订阅
    if (event->subscribe.attr_handle == my_custom_chr_val_handle)
    {
        custom_chr_conn_handle = event->subscribe.conn_handle;
        custom_notify_enabled = event->subscribe.cur_notify || event->subscribe.cur_indicate;

        ESP_LOGI(TAG, "Custom characteristic notify/indicate updated: %d", custom_notify_enabled);
    }
}

/*
 *  GATT server initialization
 *      1. Initialize GATT service
 *      2. Update NimBLE host GATT services counter
 *      3. Add GATT services to server
 */
int gatt_svc_init(void)
{
    /* Local variables */
    int rc;

    /* 1. GATT service initialization */
    ble_svc_gatt_init();

    /* 2. Update GATT services counter */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    /* 3. Add GATT services */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0)
    {
        return rc;
    }

    return 0;
}


// data_access 增强版：加密连接才允许访问
static int data_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
#if BLE_ENCRYPTION_REQUIRED
    // 先检查连接是否加密
    if (!is_connection_encrypted(conn_handle))
    {
        ESP_LOGW(TAG, "Access denied: connection not encrypted (conn_handle=%d)", conn_handle);
        return 0; // 返回加密错误
    }
#endif

    int rc;

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        // 如果你不支持读
        ESP_LOGW(TAG, "Read operation not supported");
        return BLE_ATT_ERR_UNLIKELY;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
    {
        int len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > QUEUE_ITEM_SIZE)
            len = QUEUE_ITEM_SIZE;

        ble_data_t data = {0};
        os_mbuf_copydata(ctxt->om, 0, len, data.buf);
        data.len = len;

        if (ble_rx_queue != NULL)
        {
            if (xQueueSend(ble_rx_queue, &data, 0) != pdPASS)
            {
                ESP_LOGW(TAG, "Failed to write data to RX queue");
            }
            else
            {
                ESP_LOGI(TAG, "Data written to RX queue: %.*s", len, data.buf);
            }
        }
        return 0; // 写入成功
    }

    default:
        ESP_LOGE(TAG, "Unknown operation: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}


/*******************************************************************************
****@brief: 发送任务：从发送队列中读取数据，并通过BLE通知发送给客户端
****@param: *param: FreeRTOS 任务参数（未使用）
****@author: Luo
****@date: 2025-08-27 08:18:13
********************************************************************************/
void ble_send_task(void *param)
{
    ble_data_t data;
    while (1)
    {
        // 只有在客户端订阅(characteristic notify)后，custom_notify_enabled 才为 true
        if (custom_chr_conn_handle && custom_notify_enabled)
        {
            // 从发送队列中阻塞等待数据（直到有数据进入队列才继续）
            if (xQueueReceive(ble_tx_queue, &data, portMAX_DELAY) == pdTRUE)
            {
                // 分配 os_mbuf 缓冲区，用于封装要发送的数据
                struct os_mbuf *om = ble_hs_mbuf_from_flat(data.buf, data.len);
                if (om == NULL)
                {
                    ESP_LOGE(TAG, "Failed to allocate mbuf");
                    continue; // 分配失败则跳过本次循环
                }

                // 通过自定义特征值发送通知给客户端
                int rc = ble_gatts_notify_custom(custom_chr_conn_handle,
                                                 my_custom_chr_val_handle, om);
                if (rc != 0)
                {
                    ESP_LOGE(TAG, "Notify send failed, rc=%d", rc);
                }
                else
                {
                    ESP_LOGI(TAG, "Notify sent: %.*s", (int)data.len, data.buf);
                }
            }
        }
        else
        {
            // 没有连接或客户端未订阅时，延时等待，避免空转占用CPU
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}


/*******************************************************************************
****@brief: 接收任务：从接收队列读取数据，处理后生成响应并发送回客户端
****@param: *param: FreeRTOS 任务参数（未使用）
****@author: Luo
****@date: 2025-08-27 08:18:27
********************************************************************************/
void ble_receive_task(void *param)
{
    ble_data_t data;

    while (1)
    {
        // 从接收队列阻塞等待数据（直到有数据被放入队列）
        if (xQueueReceive(ble_rx_queue, &data, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "Received data: %.*s", (int)data.len, data.buf);

            // 调用协议解析函数，生成响应数据（写入 response 缓冲区）
            memset(response, 0, sizeof(response));
            ctrl_protocol((char *)data.buf, response, sizeof(response));

            // 如果协议处理有输出（response 非空），则放入发送队列
            if (strlen(response) > 0 && ble_tx_queue != NULL)
            {
                ble_data_t tx_data = {0};
                strncpy((char *)tx_data.buf, response, QUEUE_ITEM_SIZE - 1);
                tx_data.len = strnlen((char *)tx_data.buf, QUEUE_ITEM_SIZE);

                // 将响应数据放入发送队列，准备由 ble_send_task 发送
                if (xQueueSend(ble_tx_queue, &tx_data, 10 / portTICK_PERIOD_MS) != pdPASS)
                {
                    ESP_LOGW(TAG, "Send queue full, response dropped");
                }
                else
                {
                    ESP_LOGI(TAG, "Response queued for sending: %s", tx_data.buf);
                }
            }
        }
    }
}
