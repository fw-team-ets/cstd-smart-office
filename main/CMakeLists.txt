idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES board logx storage supervisor i2cbus relay
                                display tps25751 eth netmon pairing api
                       EMBED_TXTFILES "certs/tls_server_cert.pem"
                                      "certs/tls_server_key.pem")
