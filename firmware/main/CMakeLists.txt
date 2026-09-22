# certs/*.pem are embedded by components/api (the only consumer of the
# _binary_tls_server_*_pem_* symbols) — see the comment there. The files
# still live in this directory (main/certs/); only the EMBED_TXTFILES
# declaration moved, to fix a libapi.a/libmain.a link-order problem.
idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES board logx storage supervisor i2cbus relay
                                display tps25751 eth netmon pairing api)
