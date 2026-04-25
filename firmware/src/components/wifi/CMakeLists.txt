idf_component_register(
    SRCS "Wifi.cpp"
    INCLUDE_DIRS "include"
    REQUIRES storage esp_wifi esp_event esp_netif
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
