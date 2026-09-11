#include "isobus/vt_app.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "esp_log.h"
#include "io/buzzer_driver.hpp"
#include "io/relay_driver.hpp"
#include "isobus/isobus/can_NAME.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_partnered_control_function.hpp"
#include "isobus/isobus/isobus_virtual_terminal_client.hpp"
#include "isobus/object_pool_ids.hpp"
#include "isobus/utility/iop_file_interface.hpp"

// Symbols for the object pool binary embedded via main/CMakeLists.txt's
// EMBED_FILES (linker-generated, matching the object pool's filename).
extern "C" const uint8_t object_pool_iop_start[] asm("_binary_object_pool_iop_start");
extern "C" const uint8_t object_pool_iop_end[] asm("_binary_object_pool_iop_end");

namespace iso::vt_app {

namespace {
constexpr const char* kTag = "vt_app";
constexpr uint8_t kColourBlack = 0;

std::shared_ptr<isobus::VirtualTerminalClient> g_vt_client;

// The relay driver is the one source of truth for relay state (per
// docs/vt-ui-design.md's precedence rule: last action wins, no input path
// is more authoritative than another). Every path that changes a relay --
// SKM press or AUX-N function -- funnels through here so the VT's Data
// Mask indicator always reflects reality. Returns false (and leaves the
// relay untouched) if the I2C write itself failed.
bool apply_relay_state(int channel, bool new_state) {
    if (!io::relay_driver::set_relay(channel, new_state)) {
        ESP_LOGE(kTag, "relay %d set_relay failed", channel);
        return false;
    }
    ESP_LOGI(kTag, "relay %d -> %s", channel, new_state ? "ON" : "OFF");
    g_vt_client->send_change_fill_attributes(
        object_pool_ids::relay_fill_attr_id(channel),
        new_state ? isobus::VirtualTerminalClient::FillType::FillWithSpecifiedColourInFillColourAttribute
                   : isobus::VirtualTerminalClient::FillType::NoFill,
        kColourBlack, isobus::NULL_OBJECT_ID);
    return true;
}

void handle_soft_key_event(const isobus::VirtualTerminalClient::VTKeyEvent& event) {
    if (event.keyEvent != isobus::VirtualTerminalClient::KeyActivationCode::ButtonUnlatchedOrReleased) {
        return;  // act on release, like a normal button click
    }

    for (int ch = 1; ch <= 8; ++ch) {
        if (event.objectID == object_pool_ids::softkey_id(ch)) {
            apply_relay_state(ch, !io::relay_driver::get_relay(ch));
            return;
        }
    }

    if (event.objectID == object_pool_ids::softkey_id(9)) {
        ESP_LOGI(kTag, "SK9: buzzer pulse");
        io::buzzer_driver::pulse();
    }
}

// AUX-N: a latching input reports its own persisted 0/1 state on change; a
// momentary input reports 1 only while held, 0 on release. Both function
// variants apply the reported value straight to the relay -- the "feel"
// comes entirely from how the assigned input device reports its value
// over time, not from anything we do differently here (see
// docs/vt-ui-design.md#aux-n-functions-17-total).
void handle_aux_function_event(const isobus::VirtualTerminalClient::AuxiliaryFunctionEvent& event) {
    const bool state = (event.value1 != 0);
    const uint16_t function_id = event.function.functionObjectID;

    for (int ch = 1; ch <= 8; ++ch) {
        if (function_id == object_pool_ids::aux_latch_function_id(ch) ||
            function_id == object_pool_ids::aux_momentary_function_id(ch)) {
            if (io::relay_driver::get_relay(ch) != state) {
                apply_relay_state(ch, state);
            }
            return;
        }
    }

    if (function_id == object_pool_ids::kAuxBuzzerFunction) {
        // Edge-triggered: fire once per press, not once per status message
        // received while the mapped input stays held.
        static bool last_buzzer_input_state = false;
        if (state && !last_buzzer_input_state) {
            ESP_LOGI(kTag, "AUX-N buzzer function: pulse");
            io::buzzer_driver::pulse();
        }
        last_buzzer_input_state = state;
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

    const uint32_t pool_size = static_cast<uint32_t>(object_pool_iop_end - object_pool_iop_start);
    // Content-hashed, not hand-bumped: the VT caches pools by this label,
    // so a stale hardcoded string here would make it silently keep serving
    // an old cached pool after we change the generator.
    const std::string pool_version =
        isobus::IOPFileInterface::hash_object_pool_to_version(object_pool_iop_start, pool_size);

    g_vt_client = std::make_shared<isobus::VirtualTerminalClient>(vt_partner, internal_ecu);
    g_vt_client->set_object_pool(0, object_pool_iop_start, pool_size, pool_version);
    g_vt_client->get_vt_soft_key_event_dispatcher().add_listener(handle_soft_key_event);
    g_vt_client->get_auxiliary_function_event_dispatcher().add_listener(handle_aux_function_event);
    g_vt_client->initialize(true);
    ESP_LOGI(kTag, "VT client started, waiting for a Virtual Terminal on the bus...");
}

}  // namespace iso::vt_app
