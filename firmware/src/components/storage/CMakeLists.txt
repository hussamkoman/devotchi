idf_component_register(
    SRCS "storage.c" "storage_pets.c"
    INCLUDE_DIRS "include"
    REQUIRES core
    PRIV_REQUIRES nvs_flash
)
