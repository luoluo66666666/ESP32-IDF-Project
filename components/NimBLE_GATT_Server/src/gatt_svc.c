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


/* --------------------------- 定义是否启用 BLE 加密访问 --------------------------- */
#define BLE_ENCRYPTION_REQUIRED 0  // 1：启用加密访问  0：不要求加密

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
static int heart_rate_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int led_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);

/* Private variables */
/* Heart rate service */
// static const ble_uuid16_t heart_rate_svc_uuid = BLE_UUID16_INIT(0x180D);

static uint8_t heart_rate_chr_val[2] = {0};
static uint16_t heart_rate_chr_val_handle;
// static const ble_uuid16_t heart_rate_chr_uuid = BLE_UUID16_INIT(0x2A37);

static uint16_t heart_rate_chr_conn_handle = 0;
bool heart_rate_chr_conn_handle_inited = false;
bool heart_rate_ind_status = false;
uint16_t custom_chr_conn_handle = 0;
bool custom_notify_enabled = false;


/* Automation IO service */
// static const ble_uuid16_t auto_io_svc_uuid = BLE_UUID16_INIT(0x1815);
// static uint16_t led_chr_val_handle;
// static const ble_uuid128_t led_chr_uuid =
    // BLE_UUID128_INIT(0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef,
                    //  0x12, 0x12, 0x25, 0x15, 0x00, 0x00);

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
#define MY_CUSTOM_FLAGS   (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY | \
                           BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC)
#else
#define MY_CUSTOM_FLAGS   (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY)
#endif

/* GATT services table */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {

    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &my_custom_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){/* LED characteristic */
                                        {.uuid = &my_custom_chr_uuid.u,
                                         .access_cb = data_access,
                                         .flags = MY_CUSTOM_FLAGS,
                                         .val_handle = &my_custom_chr_val_handle},
                                        {0}},
    },

    {
        0, /* No more services. */
    } /* End of services table */
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


// 判断连接是否加密
bool is_connection_encrypted(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_conn_find failed with %d", rc);
        return false;
    }
    return desc.sec_state.encrypted;
}

// data_access 增强版：加密连接才允许访问
static int data_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
#if BLE_ENCRYPTION_REQUIRED
    // 先检查连接是否加密
    if (!is_connection_encrypted(conn_handle)) {
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


// 发送任务：从发送队列中读取数据，并通过BLE通知发送给客户端
void ble_send_task(void *param)
{
    ble_data_t data;
    while (1)
    {
        // 判断是否已初始化连接句柄且通知已启用
        ESP_LOGI(TAG, "Conn inited: %d, Notify enabled: %d", custom_chr_conn_handle, custom_notify_enabled);
        if (custom_chr_conn_handle && custom_notify_enabled)
        {
            // 阻塞等待发送队列数据
            if (xQueueReceive(ble_tx_queue, &data, portMAX_DELAY) == pdTRUE)
            {
                // 创建mbuf结构存放发送数据
                struct os_mbuf *om = ble_hs_mbuf_from_flat(data.buf, data.len);
                if (om == NULL)
                {
                    ESP_LOGE(TAG, "Failed to allocate mbuf");
                    continue;
                }
                // 发送通知
                int rc = ble_gatts_notify_custom(custom_chr_conn_handle, my_custom_chr_val_handle, om);;
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
            // 未初始化时，任务延时等待
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}


// 接收任务：从接收队列读取数据，处理后生成响应并发送回客户端
void ble_receive_task(void *param)
{
    ble_data_t data;
    char response[QUEUE_ITEM_SIZE];

    while (1)
    {
        // 从接收队列读取数据
        if (xQueueReceive(ble_rx_queue, &data, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "Received data: %.*s", (int)data.len, data.buf);

            // 调用协议处理函数，生成响应
            memset(response, 0, sizeof(response));
            ctrl_protocol((char *)data.buf, response, sizeof(response));

            // 如果响应不为空，则放入发送队列
            if (strlen(response) > 0 && ble_tx_queue != NULL)
            {
                ble_data_t tx_data = {0};
                strncpy((char *)tx_data.buf, response, QUEUE_ITEM_SIZE - 1);
                tx_data.len = strnlen((char *)tx_data.buf, QUEUE_ITEM_SIZE);

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

    vTaskDelete(NULL);
}


