idf_component_register(
    SRCS "Protocol.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bblanchon__arduinojson
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
