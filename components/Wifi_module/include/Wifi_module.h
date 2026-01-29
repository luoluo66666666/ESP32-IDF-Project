#include <esp_event_base.h>

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);
                            
void Wifi_task(void);

void wifi_module_queue_init(void);

void mywifi_log(const char *fmt, ...);

void wifi_tcp_start(void);

/**
 * OTA 专用 WiFi 入口（更友好的 OTA 模式接口）
 * - 如果 WiFi 未启动，函数会初始化并启动 STA；如果已启动则直接等待连接
 * - 获取 IP 后启动 HTTP OTA 任务（调用 http_ota_start）
 * @param default_url OTA 默认固件 URL，可为 NULL
 */
void wifi_ota_mode_start(const char *default_url);

/**
 * 停止 OTA 模式启动的资源（如果 OTA 模式负责启动 WiFi，则可停止 WiFi）
 * 注意：如果 WiFi 被其他模块使用，此函数不会停止全局 WiFi
 */
void wifi_ota_mode_stop(void);
