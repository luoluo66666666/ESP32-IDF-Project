/**
 * @file Wifi_module.h
 * @brief WiFi 模块对外接口（实现见 Wifi_manage.c）
 *
 * 典型用法：main 里调用 wifi_tcp_start() 即可。
 * 配网/状态命令经 ctrl_protocol() → wifi_module_handle_config_command() 处理。
 *
 * =============================================================================
 * 配网 / 远程改配置（BLE、AP 本地 TCP、STA 云端 TCP 三通道相同，发什么回什么）
 * =============================================================================
 *
 * 【读配置】
 *   CFG:GET
 *
 * 【改店铺 WiFi】
 *   CFG:WIFI_SSID=新SSID
 *   CFG:WIFI_PASSWORD=新密码
 *   CFG:APPLY          保存并重连；失败 30s 回滚旧 WiFi
 *
 * 【改云端 TCP 地址/端口】
 *   CFG:SERVER_IP=新IP
 *   CFG:SERVER_PORT=新端口
 *   CFG:APPLY          保存并断开当前 TCP，连新地址
 *
 * 【只写入 NVS 不立刻重连】
 *   CFG:SAVE
 *
 * 【查状态】
 *   SYS:STATUS
 *
 * 【退回手机热点配网】
 *   SYS:MODE=AP
 *
 * 【查设备码 / 芯片 SN】
 *   CMD:SN_GET / CMD:SN
 *   连接时也会主动上报（云端 REG|SN|版本，本地/BLE CMD:SN,OK,SN=...）
 *
 * 【远程 OTA 固件升级】（需 STA 已连 WiFi；地址由用户填写，无固件默认值）
 *   CFG:OTA_URL=http://你的服务器/路径/固件.bin
 *   CMD:OTA
 *   CMD:OTA=http://服务器/其他.bin
 *
 * handle_ota_command() — OTA 远程升级
 * handle_config_command() / handle_system_command() — 配网
 * 实现位置：Wifi_manage.c
 */

#include <stdbool.h>

/* 创建云端 TCP 用的收发队列（wifi_tcp_start 内部也会调用） */
void wifi_module_queue_init(void);

/* WiFi 模块日志打印（可变参数，行为同 printf） */
void mywifi_log(const char *fmt, ...);

/* 获取 ESP32 芯片 MAC 派生的设备码（SN_XXXXXXXXXXXX） */
const char *wifi_module_get_device_sn(void);

/* 云端 TCP 是否已连接（STA 工作模式） */
bool wifi_module_tcp_is_connected(void);

/* 向云端 TCP 主动推送一行文本（如 DI,DI1=1）；未连接时丢弃 */
void wifi_module_tcp_push_line(const char *line);

/*
 * 启动 WiFi 模块
 * - 根据 NVS 配网标志 wifi_prov 决定上电进 AP 或 STA
 * - 已配网则自动连 NVS 里保存的路由器和云端 TCP
 * - 创建 wifi_protocol_task、tcp_client_task
 */
void wifi_tcp_start(void);

/*
 * 处理 CFG:* / SYS:* / CMD:OTA 命令
 * input  — 命令字符串，如 "CFG:GET"、"SYS:STATUS"、"CMD:OTA"
 * output — 应答写入缓冲区
 * maxlen — output 最大字节数
 * 返回值 — 已识别并处理返回 true，否则 false
 *
 * 供 ctrl_protocol() 调用；BLE / AP TCP / 云端 TCP 共用此入口
 */
bool wifi_module_handle_config_command(const char *input, char *output, int maxlen);

/*
 * 进入 OTA 模式
 * default_url — 固件下载 URL，可为 NULL
 * 若 WiFi 未启动会先初始化；STA 拿到 IP 后启动 HTTP OTA 任务
 */
void wifi_ota_mode_start(const char *default_url);

static void tcp_sock_send_line(int sock, const void *data, size_t data_len);
/*
 * 退出 OTA 模式
 * 若 WiFi 由 OTA 流程启动，会停止 WiFi；否则不影响其他模块占用的 WiFi
 */
void wifi_ota_mode_stop(void);
