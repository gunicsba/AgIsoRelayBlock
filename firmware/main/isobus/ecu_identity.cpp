#include "isobus/ecu_identity.hpp"

#include <string>

#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "isobus/hardware_integration/can_hardware_interface.hpp"
#include "isobus/hardware_integration/twai_plugin.hpp"
#include "isobus/isobus/can_NAME.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_stack_logger.hpp"

namespace iso::ecu_identity {

namespace {
constexpr gpio_num_t kTxPin = GPIO_NUM_17;
constexpr gpio_num_t kRxPin = GPIO_NUM_18;
constexpr const char* kTag = "ecu_identity";

// AgIsoStack++'s maintainers allow non-commercial use of their SAE
// manufacturer code -- see docs/isobus-protocol.md. Revisit if this project
// is ever sold/productized (would need our own manufacturer code instead).
constexpr uint16_t kManufacturerCode = 1407;

// Routes the CAN stack's own internal logging through ESP-IDF's logger
// instead of stdout, so it interleaves correctly with our own log lines.
class EspLogStackLogger : public isobus::CANStackLogger {
public:
    void sink_CAN_stack_log(LoggingLevel level, const std::string& text) override {
        switch (level) {
            case LoggingLevel::Debug:
                ESP_LOGD(kTag, "%s", text.c_str());
                break;
            case LoggingLevel::Info:
                ESP_LOGI(kTag, "%s", text.c_str());
                break;
            case LoggingLevel::Warning:
                ESP_LOGW(kTag, "%s", text.c_str());
                break;
            case LoggingLevel::Error:
            case LoggingLevel::Critical:
            default:
                ESP_LOGE(kTag, "%s", text.c_str());
                break;
        }
    }
};

EspLogStackLogger g_stack_logger;
}  // namespace

std::shared_ptr<isobus::InternalControlFunction> init() {
    isobus::CANStackLogger::set_can_stack_logger_sink(&g_stack_logger);
    isobus::CANStackLogger::set_log_level(isobus::CANStackLogger::LoggingLevel::Info);

    twai_general_config_t twai_config =
        TWAI_GENERAL_CONFIG_DEFAULT(kTxPin, kRxPin, TWAI_MODE_NORMAL);
    // ISO 11783-2 bus speed.
    twai_timing_config_t twai_timing = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t twai_filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    auto can_driver =
        std::make_shared<isobus::TWAIPlugin>(&twai_config, &twai_timing, &twai_filter);

    isobus::CANHardwareInterface::set_number_of_can_channels(1);
    isobus::CANHardwareInterface::assign_can_channel_frame_handler(0, can_driver);

    if (!isobus::CANHardwareInterface::start() || !can_driver->get_is_valid()) {
        ESP_LOGE(kTag, "failed to start CAN hardware interface / TWAI driver");
        return nullptr;
    }

    isobus::NAME device_name(0);
    device_name.set_arbitrary_address_capable(true);
    device_name.set_industry_group(
        static_cast<uint8_t>(isobus::NAME::IndustryGroup::AgriculturalAndForestryEquipment));
    device_name.set_device_class(static_cast<uint8_t>(isobus::NAME::DeviceClass::NonSpecific));
    // "Reporting and/or control unit for external input and output
    // channels" -- the closest standard fit for a generic relay/DI block
    // that isn't tied to one specific implement type (planter, sprayer,
    // etc.), matching this project's scope (see docs/isobus-protocol.md).
    device_name.set_function_code(static_cast<uint8_t>(isobus::NAME::Function::IOController));
    device_name.set_ecu_instance(0);
    device_name.set_function_instance(0);
    device_name.set_device_class_instance(0);
    device_name.set_manufacturer_code(kManufacturerCode);

    // Identity number just needs to be unique among devices sharing our
    // manufacturer code; derive it from this chip's factory-programmed MAC
    // so every unit differs with no user configuration step. Masked to 21
    // bits to match the NAME field width (avoids a spurious range warning
    // from the setter; uniqueness in practice is unaffected).
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t identity = ((static_cast<uint32_t>(mac[3]) << 16) |
                         (static_cast<uint32_t>(mac[4]) << 8) |
                         static_cast<uint32_t>(mac[5])) &
                        0x1FFFFF;
    device_name.set_identity_number(identity);

    // Preferred (not exclusive) address, derived the same deterministic
    // way as identity so it's stable across reboots rather than picking
    // whatever's free each time (the default when no preferred address is
    // given). Address instability was a suspected contributor to the VT
    // needing a manual restart to notice this device again -- its own
    // per-client bookkeeping may not expect the same NAME to reappear at a
    // different address. Still falls back to normal dynamic arbitration
    // (arbitrary_address_capable=true) if 128+ is somehow already taken.
    uint8_t preferred_address = static_cast<uint8_t>(128 + (identity % 100));

    auto internal_ecu = isobus::CANNetworkManager::CANNetwork.create_internal_control_function(
        device_name, 0, preferred_address);
    ESP_LOGI(kTag, "claiming ISOBUS address (identity=0x%06lX, preferred=%u)...",
             static_cast<unsigned long>(identity), preferred_address);
    return internal_ecu;
}

}  // namespace iso::ecu_identity
