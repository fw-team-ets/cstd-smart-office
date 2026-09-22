idf_component_register(SRCS "eth.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_eth esp_netif esp_event driver mdns esp_hw_support
                                board logx storage)
