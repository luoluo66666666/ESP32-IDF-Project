# Embed CA, certificate & key directly into binary
idf_component_register(
    SRCS 
        "Pro_Dis.c"
    INCLUDE_DIRS 
        "."
    REQUIRES
        driver
        esp_timer
)