/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "gap.h"
#include "common.h"
#include "gatt_svc.h"
#include "heart_rate.h"

#include <ctrl_protocol.h> // Include the ctrl_protocol header for ctrl_protocol functions

/* Private function declarations */
inline static void format_addr(char *addr_str, uint8_t addr[]);
static void print_conn_desc(struct ble_gap_conn_desc *desc);
static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

/* 加入安全宏定义 */
#ifndef ENABLE_SECURITY
    #define ENABLE_SECURITY 0   // 1 表示启用安全，0 表示禁用

#endif


/* Private variables */
static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};
static uint8_t esp_uri[] = {BLE_GAP_URI_PREFIX_HTTPS, '/', '/', 'e', 's', 'p', 'r', 'e', 's', 's', 'i', 'f', '.', 'c', 'o', 'm'};

extern void ble_send_task(void *param);
extern void ble_receive_task(void *param);

/* Private functions */
inline static void format_addr(char *addr_str, uint8_t addr[])
{
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1],
            addr[2], addr[3], addr[4], addr[5]);
}

static void print_conn_desc(struct ble_gap_conn_desc *desc)
{
    /* Local variables */
    char addr_str[18] = {0};

    /* Connection handle */
    ESP_LOGI(TAG, "connection handle: %d", desc->conn_handle);

    /* Local ID address */
    format_addr(addr_str, desc->our_id_addr.val);
    ESP_LOGI(TAG, "device id address: type=%d, value=%s",
             desc->our_id_addr.type, addr_str);

    /* Peer ID address */
    format_addr(addr_str, desc->peer_id_addr.val);
    ESP_LOGI(TAG, "peer id address: type=%d, value=%s", desc->peer_id_addr.type,
             addr_str);

    /* Connection info */
    ESP_LOGI(TAG,
             "conn_itvl=%d, conn_latency=%d, supervision_timeout=%d, "
             "encrypted=%d, authenticated=%d, bonded=%d\n",
             desc->conn_itvl, desc->conn_latency, desc->supervision_timeout,
             desc->sec_state.encrypted, desc->sec_state.authenticated,
             desc->sec_state.bonded);
}

/* 启动 BLE 广播 */
static void start_advertising(void)
{
    /* 本地变量 */
    int rc = 0;
    const char *name;
    struct ble_hs_adv_fields adv_fields = {0};  // 广播数据
    struct ble_hs_adv_fields rsp_fields = {0};  // 扫描响应数据
    struct ble_gap_adv_params adv_params = {0}; // 广播参数

    /* 1. 设置广播标志 */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN       // 一般可发现模式（General Discoverable Mode）
                     | BLE_HS_ADV_F_BREDR_UNSUP;   // 不支持 BR/EDR（纯 BLE 设备）

    /* 2. 设置设备名称 */
    name = ble_svc_gap_device_name();     // 从 GAP 服务获取设备名
    adv_fields.name = (uint8_t *)name;    // 设置名字指针
    adv_fields.name_len = strlen(name);   // 设置名字长度
    adv_fields.name_is_complete = 1;      // 表示完整名称（而不是缩写）

    /* 3. 设置发射功率 */
    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO; // 自动选择 Tx power
    adv_fields.tx_pwr_lvl_is_present = 1;               // 表示包含该字段

    /* 4. 设置设备外观 (Appearance) */
    adv_fields.appearance = BLE_GAP_APPEARANCE_GENERIC_TAG; // 使用 "Generic Tag" 外观
    adv_fields.appearance_is_present = 1;                   // 表示包含该字段

    /* 5. 设置设备 LE role */
    adv_fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL; // 设置为 Peripheral（外设角色）
    adv_fields.le_role_is_present = 1;

    /* 6. 提交广播数据 */
    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set advertising data, error code: %d", rc);
        return;
    }

    /* 7. 设置扫描响应字段 */
    rsp_fields.device_addr = addr_val;               // 设备地址（全局变量）
    rsp_fields.device_addr_type = own_addr_type;     // 地址类型（public 或 random）
    rsp_fields.device_addr_is_present = 1;

    rsp_fields.uri = esp_uri;                        // 设置 URI (例如 "https://...")
    rsp_fields.uri_len = sizeof(esp_uri);

    rsp_fields.adv_itvl = BLE_GAP_ADV_ITVL_MS(500);  // 广播间隔
    rsp_fields.adv_itvl_is_present = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set scan response data, error code: %d", rc);
        return;
    }

    /* 8. 设置广播参数 */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // 不定向广播（Undirected, 可被任何中心连接）
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // 一般可发现模式

    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(500); // 最小广播间隔 500ms
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(510); // 最大广播间隔 510ms

    /* 9. 启动广播 */
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params,       // 广播参数
                           gap_event_handler, // GAP 事件回调
                           NULL);             // 用户参数
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to start advertising, error code: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "advertising started!");
}



// 判断设备是否已配对
#define MAX_BONDED_DEVICES 10
static ble_addr_t bonded_devices[MAX_BONDED_DEVICES];
static int bonded_count = 0;
static bool is_device_bonded(const ble_addr_t *addr)
{
    for (int i = 0; i < bonded_count; i++) {
        if (bonded_devices[i].type == addr->type &&
            memcmp(bonded_devices[i].val, addr->val, 6) == 0) {
            return true;
        }
    }
    return false;
}


// 添加设备到已配对列表
static void add_bonded_device(const ble_addr_t *addr)
{
    if (bonded_count < MAX_BONDED_DEVICES) {
        bonded_devices[bonded_count++] = *addr;
        ESP_LOGI(TAG, "Added bonded device %02X:%02X:%02X:%02X:%02X:%02X, total %d",
                 addr->val[5], addr->val[4], addr->val[3],
                 addr->val[2], addr->val[1], addr->val[0], bonded_count);
    } else {
        ESP_LOGW(TAG, "Bonded device list full");
    }
}


/*******************************************************************************
****@brief:  * GAP 事件回调函数
 * - NimBLE 使用事件驱动模型，当 GAP 层有事件发生时会调用该回调
 * - 例如：连接成功/断开、加密、配对、订阅通知、MTU 更新等
****@param: *event:
****@param: *arg:
****@author: Luo
****@date: 2025-08-27 08:22:54
********************************************************************************/
static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    int rc = 0;
    struct ble_gap_conn_desc desc; // 用来存储连接的详细信息（间隔、超时、对端地址等）

    switch (event->type)
    {
    /* ========== 1. 连接事件 ========= */
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0) // 连接成功
        {
            // 获取连接的描述信息
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "failed to find connection by handle, error code: %d", rc);
                return rc;
            }

            // 打印连接参数（地址、间隔、超时等）
            print_conn_desc(&desc);

            // 更新连接参数（保持相同间隔，但允许从机延迟=3）
            struct ble_gap_upd_params params = {
                .itvl_min = desc.conn_itvl,
                .itvl_max = desc.conn_itvl,
                .latency = 3,
                .supervision_timeout = desc.supervision_timeout
            };
            rc = ble_gap_update_params(event->connect.conn_handle, &params);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "failed to update connection parameters, error code: %d", rc);
                return rc;
            }

#if ENABLE_SECURITY
            /* 如果启用了安全模式，检查是否已绑定（bonded） */
            bool bonded = is_device_bonded(&desc.peer_id_addr);
            if (bonded)
            {
                ESP_LOGI(TAG, "Device is bonded, skip security initiation.");
            }
            else
            {
                ESP_LOGI(TAG, "Device is NOT bonded, initiate security.");
                rc = ble_gap_security_initiate(event->connect.conn_handle);
                if (rc != 0)
                {
                    ESP_LOGE(TAG, "failed to initiate security, rc=%d", rc);
                }
                // 如果成功配对，会在 BLE_GAP_EVENT_ENC_CHANGE 中保存 bond 信息
            }
#else
            ESP_LOGI(TAG, "Security disabled: no pairing required.");
#endif
        }
        else
        {
            // 连接失败，重新开始广播
            start_advertising();
        }
        return rc;

    /* ========== 2. 断开连接事件 ========= */
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected from peer; reason=%d", event->disconnect.reason);
        // 自动重新进入广播
        start_advertising();
        return rc;

    /* ========== 3. 连接参数更新 ========= */
    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "connection updated; status=%d", event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "failed to find connection by handle, error code: %d", rc);
            return rc;
        }
        print_conn_desc(&desc);
        return rc;

    /* ========== 4. 广播完成 ========= */
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertise complete; reason=%d", event->adv_complete.reason);
        // 重新开始广播
        start_advertising();
        return rc;

    /* ========== 5. 通知发送完成 ========= */
    case BLE_GAP_EVENT_NOTIFY_TX:
        if ((event->notify_tx.status != 0) && (event->notify_tx.status != BLE_HS_EDONE))
        {
            ESP_LOGI(TAG, "notify event; conn_handle=%d attr_handle=%d status=%d is_indication=%d",
                     event->notify_tx.conn_handle, event->notify_tx.attr_handle,
                     event->notify_tx.status, event->notify_tx.indication);
        }
        return rc;

    /* ========== 6. 客户端订阅通知/指示 ========= */
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d reason=%d prevn=%d curn=%d previ=%d curi=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle,
                 event->subscribe.reason, event->subscribe.prev_notify,
                 event->subscribe.cur_notify, event->subscribe.prev_indicate,
                 event->subscribe.cur_indicate);
        // 调用 GATT 层的订阅回调，启用/关闭通知
        gatt_svr_subscribe_cb(event);
        return rc;

    /* ========== 7. MTU 更新 ========= */
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.channel_id,
                 event->mtu.value);
        return rc;

#if ENABLE_SECURITY
    /* ========== 8. 加密状态变化（配对/加密完成） ========= */
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0)
        {
            ESP_LOGI(TAG, "connection encrypted!");
            rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            if (rc == 0)
            {
                // 把该设备加入已绑定设备列表
                add_bonded_device(&desc.peer_id_addr);
            }
        }
        else
        {
            ESP_LOGE(TAG, "connection encryption failed, status: %d", event->enc_change.status);
        }
        return rc;

    /* ========== 9. 重复配对请求 ========= */
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "failed to find connection, error code %d", rc);
            return rc;
        }
        // 删除旧的 bond 信息，允许重新配对
        ble_store_util_delete_peer(&desc.peer_id_addr);
        ESP_LOGI(TAG, "Repeat pairing, deleting old bond and retrying");
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    /* ========== 10. Passkey 交互（固定密码） ========= */
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            uint32_t fixed_passkey = 123456;
            ESP_LOGI(TAG, "Use fixed passkey %06" PRIu32 " on the peer device", fixed_passkey);

            struct ble_sm_io pkey = {0};
            pkey.action = event->passkey.params.action;
            pkey.passkey = fixed_passkey;

            int rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "Failed to inject fixed passkey, rc=%d", rc);
                return rc;
            }
        }
        return 0;
#endif

    /* 其他事件：默认忽略 */
    default:
        break;
    }

    return rc;
}



/* Public functions */
void adv_init(void)
{
    /* Local variables */
    int rc = 0;
    char addr_str[18] = {0};

    /* Make sure we have proper BT identity address set (random preferred) */
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "device does not have any available bt address!");
        return;
    }

    /* Figure out BT address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to infer address type, error code: %d", rc);
        return;
    }

    /* Printing ADDR */
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to copy device address, error code: %d", rc);
        return;
    }
    format_addr(addr_str, addr_val);
    ESP_LOGI(TAG, "device address: %s", addr_str);

    /* Start advertising. */
    start_advertising();
}

/*******************************************************************************
****@brief: 通用访问规范 (Generic Access Profile, GAP init)
****@author: Luo
****@date: 2025-08-11 08:46:35
********************************************************************************/
int gap_init(void)
{
    /* Local variables */
    int rc = 0;

    /* Call NimBLE GAP initialization API */
    ble_svc_gap_init();

    /* Set GAP device name */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set device name to %s, error code: %d",
                 DEVICE_NAME, rc);
        return rc;
    }
    return rc;
}

/* Library function declarations */
void ble_store_config_init(void);

/* Private function declarations */
static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);

/* Private functions */
/*
 *  Stack event callback functions
 *      - on_stack_reset is called when host resets BLE stack due to errors
 *      - on_stack_sync is called when host has synced with controller
 */
static void on_stack_reset(int reason)
{
    /* On reset, print reset reason to console */
    ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void)
{
    /* On stack sync, do advertising initialization */
    adv_init();
}


/*******************************************************************************
****@brief:  * NimBLE Host 层配置初始化
 * - 设置各种回调函数
 * - 配置安全模式（是否启用配对/加密/绑定）
****@author: Luo
****@date: 2025-08-27 08:25:04
********************************************************************************/
static void nimble_host_config_init(void)
{
    /* 设置主机栈回调 */
    ble_hs_cfg.reset_cb = on_stack_reset;            // BLE Host 重置时调用
    ble_hs_cfg.sync_cb = on_stack_sync;              // Host 与 Controller 同步完成时调用
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb; // GATT 服务/特征注册时的回调
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr; // 存储状态回调（保存bond信息等）

#if ENABLE_SECURITY
    /* 启用安全模式（需要配对+加密） */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;   // IO 能力：仅显示（用于显示Passkey）
    ble_hs_cfg.sm_bonding = 1;                       // 启用绑定（bonding）
    ble_hs_cfg.sm_mitm = 1;                          // 启用 MITM 防护（防中间人攻击）
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    // 我方分发的密钥类型：加密密钥、身份密钥
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    // 对方分发的密钥类型：加密密钥、身份密钥
#else
    /* 禁用安全模式（无配对/加密，直连） */
    ble_hs_cfg.sm_io_cap = 0;                        // 无 IO 能力
    ble_hs_cfg.sm_bonding = 0;                       // 不绑定
    ble_hs_cfg.sm_mitm = 0;                          // 不启用 MITM
    ble_hs_cfg.sm_our_key_dist = 0;                  // 不分发任何密钥
    ble_hs_cfg.sm_their_key_dist = 0;                // 不接收任何密钥
#endif

    /* 初始化存储配置（主要用于 bond 设备信息存储） */
    ble_store_config_init();
}


/*******************************************************************************
****@brief:  * NimBLE Host 主任务
 * - nimble_port_run() 运行 BLE Host 核心任务（阻塞运行，直到调用 nimble_port_stop()）
 * - 该任务在 FreeRTOS 中作为独立线程执行
****@param: *param:
****@author: Luo
****@date: 2025-08-27 08:25:23
********************************************************************************/
static void nimble_host_task(void *param)
{
    /* 任务启动日志 */
    ESP_LOGI(TAG, "nimble host task has been started!");

    /* 运行 NimBLE Host 主循环
     * 该函数内部会处理所有 BLE 协议栈事件
     * 注意：除非调用 nimble_port_stop()，否则不会返回
     */
    nimble_port_run();

    /* 当 nimble_port_run() 退出时执行清理 */
    vTaskDelete(NULL);
}



/*******************************************************************************
****@brief: 蓝牙任务
* -为发送接收创建任务
****@author: Luo
****@date: 2025-08-11 08:36:17
********************************************************************************/
void ble_task(void)
{
    /* Local variables */
    int rc;
    esp_err_t ret;

    /*
     * NVS flash initialization
     * Dependency of BLE stack to store configurations
     */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize nvs flash, error code: %d ", ret);
        return;
    }

    /* NimBLE stack initialization */
    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ",
                 ret);
        return;
    }

    /* GAP service initialization */
    rc = gap_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to initialize GAP service, error code: %d", rc);
        return;
    }

    /* GATT server initialization */
    rc = gatt_svc_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to initialize GATT server, error code: %d", rc);
        return;
    }

    /* NimBLE host configuration initialization */
    nimble_host_config_init();

    /* 蓝牙两个队列的初始化 */
    ble_queue_init();

    /* Start task thread and return */
    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);

    xTaskCreate(ble_send_task, "ble_send_task", 4096, NULL, 5, NULL);

    xTaskCreate(ble_receive_task, "ble_receive_task", 4096, NULL, 5, NULL);


    return;
}


