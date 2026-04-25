idf_component_register(
    SRCS "Feedback.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config driver
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
