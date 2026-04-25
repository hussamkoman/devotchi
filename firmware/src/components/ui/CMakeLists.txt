idf_component_register(
    SRCS
        "Ui.cpp"
        "screens/PetScreen.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES display buttons protocol
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
