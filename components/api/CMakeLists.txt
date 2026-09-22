idf_component_register(SRCS "api.c" "health.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_https_server json esp_timer lwip esp_netif esp_hw_support
                                board logx storage pairing relay display
                                tps25751 eth netmon supervisor)
