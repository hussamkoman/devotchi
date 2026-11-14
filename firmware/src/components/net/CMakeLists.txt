idf_component_register(
    SRCS "wifi.c" "prov.c" "tls_cfg.c" "ws.c" "pairing.c" "net_worker.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES core
    PRIV_REQUIRES storage bt esp_event esp_http_client esp_netif esp_wifi espressif__cjson
                  espressif__esp_websocket_client espressif__network_provisioning log mbedtls
)

if(CONFIG_DEVOTCHI_TLS_EXTRA_CA)
    if("${CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH}" STREQUAL "")
        message(FATAL_ERROR "CONFIG_DEVOTCHI_TLS_EXTRA_CA is set but CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH is empty")
    endif()
    get_filename_component(extra_ca_name "${CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH}" NAME)
    if(NOT extra_ca_name STREQUAL "extra_ca.pem")
        message(FATAL_ERROR "CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH must point to a file named extra_ca.pem "
                            "(got ${CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH})")
    endif()
    if(NOT EXISTS "${CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH}")
        message(FATAL_ERROR "CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH does not exist: ${CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH}")
    endif()
    target_add_binary_data(${COMPONENT_LIB} "${CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH}" TEXT)
endif()
