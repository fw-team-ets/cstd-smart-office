# EMBED_TXTFILES here, not in main/CMakeLists.txt: api.c is the only consumer
# of _binary_tls_server_{cert,key}_pem_{start,end} (see its `asm(...)` extern
# declarations). The final link has no --start-group/--end-group, and IDF's
# dependency-based ordering already puts libapi.a before libmain.a (main
# REQUIRES api) — ld's single left-to-right archive pass would then never
# find a symbol that only exists in a *later* library. Embedding the files
# in the same component that references them removes the cross-library
# dependency entirely; the .pem files themselves stay in main/certs/ (see
# main/certs/README.txt and tools/gen_certs.sh), only this declaration moved.
idf_component_register(SRCS "api.c" "health.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_https_server json esp_timer lwip esp_netif esp_hw_support
                                board logx storage pairing relay display
                                tps25751 eth netmon supervisor
                       EMBED_TXTFILES "../../main/certs/tls_server_cert.pem"
                                      "../../main/certs/tls_server_key.pem")
