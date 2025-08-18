#include <esp_event_base.h>

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);
                            
void Wifi_task(void);

void wifi_module_queue_init(void);

void mywifi_log(const char *fmt, ...);
