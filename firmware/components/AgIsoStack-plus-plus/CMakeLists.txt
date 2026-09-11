# Thin ESP-IDF component wrapper around the vendored AgIsoStack++ tree in
# ./upstream (a pinned git submodule -- see docs/architecture.md). Upstream
# ships its own plain-CMake `project(isobus ...)`, not an
# idf_component_register-based component, so it can't sit directly under
# components/ -- this wrapper add_subdirectory()s it and re-exposes its
# targets as this component's link interface.

idf_component_register(REQUIRES driver)

# Selects hardware_integration/CMakeLists.txt's TWAI (ESP32 TWAI controller)
# CANHardwarePlugin. Must be set before add_subdirectory() below.
set(CAN_DRIVER "TWAI")

add_subdirectory(upstream agisostack_upstream)

# Upstream's Isobus/HardwareIntegration/Utility targets already export their
# include directories PUBLIC, so linking is enough to pull those in too.
# Utility must be linked explicitly here too: Isobus/HardwareIntegration only
# link it PRIVATE, so its include dir doesn't propagate transitively.
target_link_libraries(${COMPONENT_LIB} INTERFACE Isobus HardwareIntegration Utility)
