idf_component_register(
    SRCS "Protocol.cpp"
    INCLUDE_DIRS "include"
    # Add JSON library here when implementing parse()/serialize_*().
    # ESP-IDF v6 moved cJSON to the component manager:
    #   add `espressif/cjson` to main/idf_component.yml then `REQUIRES cjson` here.
    # Or use ArduinoJson (`bblanchon/ArduinoJson`) for a more C++-idiomatic API.
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
