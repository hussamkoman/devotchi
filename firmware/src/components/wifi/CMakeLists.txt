idf_component_register(
    SRCS "Wifi.cpp"
    INCLUDE_DIRS "include"
    REQUIRES storage config esp_wifi esp_event esp_netif nvs_flash
             espressif__network_provisioning bt
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
