idf_component_register(SRCS "Bluetooth_module.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash esp_event driver Task_manager bt)
