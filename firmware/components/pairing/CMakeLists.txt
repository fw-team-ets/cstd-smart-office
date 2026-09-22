idf_component_register(SRCS "pairing.c"
                       INCLUDE_DIRS "include"
                       # NOTE: deliberately does NOT require `relay`. relay requires pairing (the
                       # button task idles until the device is paired), and ESP-IDF
                       # rejects a dependency cycle.
                       REQUIRES storage logx board display mbedtls esp_timer esp_hw_support)
