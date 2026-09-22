idf_component_register(SRCS "tps25751.c"
                       INCLUDE_DIRS "include"
                       REQUIRES i2cbus board logx storage supervisor esp_timer)
