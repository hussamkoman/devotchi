idf_component_register(
    SRCS "Storage.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
