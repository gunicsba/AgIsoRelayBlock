#include "isobus/diagnostics.hpp"

#include "esp_log.h"
#include "isobus/hardware_integration/can_hardware_interface.hpp"
#include "isobus/isobus/isobus_diagnostic_protocol.hpp"

namespace iso::diagnostics {

namespace {
constexpr const char* kTag = "diagnostics";

// Neither of these is an SAE-registered SPN (that requires an actual SAE
// membership/registration this project doesn't have) -- picked from
// J1939's own 520192-524287 "manufacturer assignable" reserved block so
// they can't collide with a real registered SPN on the same bus, and
// documented here as exactly that: internal placeholders, not a claim of
// official registration.
constexpr uint32_t kSpnVtConnectionLost = 520192;
constexpr uint32_t kSpnRelayFault = 520193;

std::unique_ptr<isobus::DiagnosticProtocol> g_protocol;
isobus::DiagnosticProtocol::DiagnosticTroubleCode g_dtc_vt_connection_lost(
    kSpnVtConnectionLost, isobus::DiagnosticProtocol::FailureModeIdentifier::ConditionExists,
    isobus::DiagnosticProtocol::LampStatus::AmberWarningLampSlowFlash);
isobus::DiagnosticProtocol::DiagnosticTroubleCode g_dtc_relay_fault(
    kSpnRelayFault, isobus::DiagnosticProtocol::FailureModeIdentifier::ConditionExists,
    isobus::DiagnosticProtocol::LampStatus::AmberWarningLampSlowFlash);
}  // namespace

void init(std::shared_ptr<isobus::InternalControlFunction> internal_ecu) {
    if (!internal_ecu) {
        ESP_LOGE(kTag, "no internal control function, not starting DM1 broadcast");
        return;
    }
    g_protocol = std::make_unique<isobus::DiagnosticProtocol>(internal_ecu);
    g_protocol->initialize();

    // DiagnosticProtocol has its own update() that has to be pumped
    // periodically to actually transmit DM1 -- same pattern as
    // AgIsoStack++'s own diagnostic_protocol example, reusing the shared
    // hardware-interface periodic event rather than adding yet another
    // timer/task of our own for it.
    isobus::CANHardwareInterface::get_periodic_update_event_dispatcher().add_listener(
        [] { g_protocol->update(); });

    ESP_LOGI(kTag, "DM1 diagnostics started");
}

void set_vt_connection_lost(bool active) {
    if (!g_protocol) {
        return;
    }
    g_protocol->set_diagnostic_trouble_code_active(g_dtc_vt_connection_lost, active);
}

void set_relay_fault(bool active) {
    if (!g_protocol) {
        return;
    }
    g_protocol->set_diagnostic_trouble_code_active(g_dtc_relay_fault, active);
}

}  // namespace iso::diagnostics
