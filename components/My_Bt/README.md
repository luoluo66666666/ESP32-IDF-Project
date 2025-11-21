        ble_task()
            │
            ├── nvs_flash_init()
            ├── nimble_port_init()
            ├── gap_init()
            ├── gatt_svc_init()
            ├── nimble_host_config_init()
            ├── xTaskCreate(nimble_host_task)
            └── xTaskCreate(heart_rate_task)

        nimble_host_task()
            └── nimble_port_run()
                    └── on_stack_sync() --> adv_init() --> start_advertising()

        start_advertising()
            ├── ble_gap_adv_set_fields()
            ├── ble_gap_adv_rsp_set_fields()
            └── ble_gap_adv_start()

        gap_event_handler()
            ├── CONNECT  → print_conn_desc() → ble_gap_update_params()
            ├── DISCONNECT → start_advertising()
            ├── ADV_COMPLETE → start_advertising()
            └── SUBSCRIBE → gatt_svr_subscribe_cb()
