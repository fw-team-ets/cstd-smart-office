idf_component_register(SRCS "supervisor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board logx storage esp_timer esp_system)
