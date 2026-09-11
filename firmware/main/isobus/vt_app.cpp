#include "isobus/vt_app.hpp"

#include <cstdint>
#include <vector>

#include "esp_log.h"
#include "io/buzzer_driver.hpp"
#include "io/relay_driver.hpp"
#include "isobus/isobus/can_NAME.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_partnered_control_function.hpp"
#include "isobus/isobus/isobus_virtual_terminal_client.hpp"
#include "isobus/object_pool_ids.hpp"

// Symbols for the object pool binary embedded via main/CMakeLists.txt's
// EMBED_FILES (linker-generated, matching the object pool's filename).
extern "C" const uint8_t object_pool_iop_start[] asm("_binary_object_pool_iop_start");
extern "C" const uint8_t object_pool_iop_end[] asm("_binary_object_pool_iop_end");

namespace iso::vt_app {

namespace {
constexpr const char* kTag = "vt_app";
constexpr uint8_t kColourBlack = 0;

std::shared_ptr<isobus::VirtualTerminalClient> g_vt_client;

void handle_soft_key_event(const isobus::VirtualTerminalClient::VTKeyEvent& event) {
    if (event.keyEvent != isobus::VirtualTerminalClient::KeyActivationCode::ButtonUnlatchedOrReleased) {
        return;  // act on release, like a normal button click
    }

    for (int ch = 1; ch <= 8; ++ch) {
        if (event.objectID != object_pool_ids::softkey_id(ch)) {
            continue;
        }
        // The relay driver is the one source of truth for relay state (per
        // docs/vt-ui-design.md's precedence rule: last action wins, no
        // input path is more authoritative than another) -- toggling here
        // just flips it and reflects the *new* state back to the VT.
        bool new_state = !io::relay_driver::get_relay(ch);
        if (!io::relay_driver::set_relay(ch, new_state)) {
            ESP_LOGE(kTag, "SK%d: relay %d set_relay failed", ch, ch);
            return;
        }
        ESP_LOGI(kTag, "SK%d: relay %d -> %s", ch, ch, new_state ? "ON" : "OFF");
        g_vt_client->send_change_fill_attributes(
            object_pool_ids::relay_fill_attr_id(ch),
            new_state ? isobus::VirtualTerminalClient::FillType::FillWithSpecifiedColourInFillColourAttribute
                       : isobus::VirtualTerminalClient::FillType::NoFill,
            kColourBlack, isobus::NULL_OBJECT_ID);
        return;
    }

    if (event.objectID == object_pool_ids::softkey_id(9)) {
        ESP_LOGI(kTag, "SK9: buzzer pulse");
        io::buzzer_driver::pulse();
    }
}
}  // namespace

void init(std::shared_ptr<isobus::InternalControlFunction> internal_ecu) {
    if (!internal_ecu) {
        ESP_LOGE(kTag, "no internal control function, not starting VT client");
        return;
    }

    const isobus::NAMEFilter vt_function_filter(
        isobus::NAME::NAMEParameters::FunctionCode,
        static_cast<uint8_t>(isobus::NAME::Function::VirtualTerminal));
    const std::vector<isobus::NAMEFilter> vt_filters = {vt_function_filter};
    auto vt_partner = isobus::CANNetworkManager::CANNetwork.create_partnered_control_function(0, vt_filters);

    g_vt_client = std::make_shared<isobus::VirtualTerminalClient>(vt_partner, internal_ecu);
    g_vt_client->set_object_pool(0, object_pool_iop_start,
                                 static_cast<uint32_t>(object_pool_iop_end - object_pool_iop_start),
                                 "AGRB01");
    g_vt_client->get_vt_soft_key_event_dispatcher().add_listener(handle_soft_key_event);
    g_vt_client->initialize(true);
    ESP_LOGI(kTag, "VT client started, waiting for a Virtual Terminal on the bus...");
}

}  // namespace iso::vt_app
