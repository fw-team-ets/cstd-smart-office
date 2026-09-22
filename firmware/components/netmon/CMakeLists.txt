idf_component_register(SRCS "netmon.c"
                       INCLUDE_DIRS "include"
                       REQUIRES eth relay display pairing supervisor storage
                                logx board lwip esp_timer)
