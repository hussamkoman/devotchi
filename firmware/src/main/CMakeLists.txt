idf_component_register(
    SRCS "main.cpp" "App.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        config storage protocol wifi ws_client
        display ui buttons feedback power
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
