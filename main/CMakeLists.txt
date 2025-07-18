# Embed CA, certificate & key directly into binary
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES ca.pem pac_file.pac)
