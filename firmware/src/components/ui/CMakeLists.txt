idf_component_register(
    SRCS
        "Ui.cpp"
        "UiTask.cpp"
        "screens/PetScreen.cpp"
        "screens/SplashScreen.cpp"
        "screens/ProvisioningScreen.cpp"
        "screens/ConnectingScreen.cpp"
        "screens/PairingScreen.cpp"
        "screens/ApprovalScreen.cpp"
        "screens/SettingsScreen.cpp"
        "widgets/StatusBar.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES display buttons protocol runtime config esp_timer lvgl__lvgl
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
