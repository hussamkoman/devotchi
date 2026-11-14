idf_component_register(
    SRCS "Pairing.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client config bblanchon__arduinojson
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
