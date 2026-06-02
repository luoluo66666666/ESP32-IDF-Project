/**
 * @file Wifi_module.h
 * @brief WiFi 模块对外接口（实现见 Wifi_manage.c）
 *
 * 用法：在 main 中调用 wifi_tcp_start() 启动 WiFi 与 TCP 任务。
 * 配网命令 CFG:*、系统命令 SYS:* 由 ctrl_protocol 转调
 * wifi_module_handle_config_command 处理。
 */

#include <stdbool.h>

/** 创建云端 TCP 用的收发队列（wifi_tcp_start 内部也会调用） */
void wifi_module_queue_init(void);

/** WiFi 模块日志打印（可变参数，行为同 printf） */
void mywifi_log(const char *fmt, ...);

/**
 * @brief 启动 WiFi 模块
 * @note 根据 NVS 配网标志决定上电进 AP 或 STA，并创建协议任务与 TCP 客户端任务
 */
void wifi_tcp_start(void);

/**
 * @brief 处理 CFG:* / SYS:* 命令
 * @param input  输入命令字符串
 * @param output 应答输出缓冲区
 * @param maxlen 缓冲区最大字节数
 * @return 已识别并处理返回 true，否则 false
 */
bool wifi_module_handle_config_command(const char *input, char *output, int maxlen);

/**
 * @brief 进入 OTA 模式
 * @param default_url 固件下载 URL，可为 NULL
 * @note 若 WiFi 未启动会先初始化；拿到 IP 后启动 HTTP OTA 任务
 */
void wifi_ota_mode_start(const char *default_url);

/**
 * @brief 退出 OTA 模式
 * @note 若 WiFi 由 OTA 流程启动，会停止 WiFi；否则不影响其他模块占用的 WiFi
 */
void wifi_ota_mode_stop(void);
