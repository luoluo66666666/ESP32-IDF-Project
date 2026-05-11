# Embed CA, certificate & key directly into binary
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES
                        modbus
                        Wifi_module
                        Uart_module
                        app_msg
                        Task_manager
                        ctrl_protocol
                        mode_ctrl
                        RS485
                        My_Bt
                    EMBED_TXTFILES ca.pem pac_file.pac)
