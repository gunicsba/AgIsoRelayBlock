idf_component_register(
    SRCS
        "app_main.cpp"
        "io/i2c_bus.cpp"
        "io/status_led.cpp"
        "io/relay_driver.cpp"
        "io/input_driver.cpp"
        "io/can_selftest.cpp"
        "io/i2c_scan.cpp"
        "io/buzzer_driver.cpp"
        "isobus/ecu_identity.cpp"
        "isobus/vt_app.cpp"
        "automation/interlock.cpp"
    INCLUDE_DIRS "."
    REQUIRES AgIsoStack-plus-plus
    PRIV_REQUIRES esp_hw_support esp_timer esp_app_format
    EMBED_FILES "isobus/object_pool.iop"
)

# AgIsoStack++ requires C++17.
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
