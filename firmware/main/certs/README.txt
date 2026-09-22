tls_server_cert.pem and tls_server_key.pem are generated, not committed.

Run, from the project root:

    tools/gen_certs.sh ste-<last 6 MAC hex>

They are embedded into the firmware image by main/CMakeLists.txt
(EMBED_TXTFILES), which is why there is no VFS/FAT partition in this build and
nothing has to be flashed at a second offset.

The build will not link until both files exist.
