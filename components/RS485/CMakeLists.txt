idf_component_register(
    SRCS    
        "rs485_water_valve.c"
    INCLUDE_DIRS "."
    REQUIRES 
        nvs_flash 
        driver
        My_Bt
    )
