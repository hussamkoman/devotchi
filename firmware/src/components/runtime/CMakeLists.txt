idf_component_register(
    SRCS "Task.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
