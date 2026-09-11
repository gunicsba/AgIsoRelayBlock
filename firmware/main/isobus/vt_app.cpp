#include "isobus/vt_app.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "automation/interlock.hpp"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "io/buzzer_driver.hpp"
#include "io/relay_driver.hpp"
#include "isobus/isobus/can_NAME.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/can_partnered_control_function.hpp"
#include "isobus/isobus/isobus_virtual_terminal_client.hpp"
#include "isobus/object_pool_ids.hpp"
#include "isobus/utility/iop_file_interface.hpp"
#include "net/wifi_ap.hpp"

// Symbols for the object pool binary embedded via main/CMakeLists.txt's
// EMBED_FILES (linker-generated, matching the object pool's filename).
extern "C" const uint8_t object_pool_iop_start[] asm("_binary_object_pool_iop_start");
extern "C" const uint8_t object_pool_iop_end[] asm("_binary_object_pool_iop_end");

namespace iso::vt_app {

namespace {
constexpr const char* kTag = "vt_app";
constexpr uint8_t kColourBlack = 0;

std::shared_ptr<isobus::VirtualTerminalClient> g_vt_client;
std::shared_ptr<isobus::PartneredControlFunction> g_vt_partner;

// "Momentary Override Safety" (docs/vt-ui-design.md): unfilled/off by
// default, and deliberately never persisted -- always starts false at
// boot, same as every relay (N4's safe-default philosophy applied to this
// setting too), so the interlock can never be silently bypassed by a
// power cycle. When true, ONLY the momentary override path below (AUX-N
// momentary function or an SKM page-2 momentary key) is allowed to turn a
// channel back on while its DI has it disabled; the toggle paths (SK1-8,
// the AUX-N latch variant) never bypass it regardless of this setting.
bool g_momentary_override_safety_enabled = false;

// The relay driver is the one source of truth for relay state (per
// docs/vt-ui-design.md's precedence rule: last action wins, no input path
// is more authoritative than another). Every path that changes a relay --
// SKM press or AUX-N function -- funnels through here so the VT's Data
// Mask indicator always reflects reality, and so the Phase 6 limit-switch
// interlock only needs to be enforced in this one place: an ON request is
// refused while the channel's paired DI is active (see automation/interlock.hpp),
// regardless of which control path asked for it, UNLESS the caller passes
// bypass_interlock=true -- reserved for handle_momentary_override() when
// g_momentary_override_safety_enabled is set, so an operator can
// deliberately nudge an actuator past a limit switch (see that function's
// comment). Turning OFF is never blocked either way. Returns false (and
// leaves the relay untouched) if refused by the interlock or if the I2C
// write itself failed.
bool apply_relay_state(int channel, bool new_state, bool bypass_interlock = false) {
    if (new_state && automation::interlock::is_disabled(channel)) {
        if (!bypass_interlock) {
            ESP_LOGW(kTag, "relay %d: ON request refused, disabled by its DI limit switch", channel);
            return false;
        }
        ESP_LOGW(kTag, "relay %d: ON while disabled by its DI limit switch, allowed anyway -- Momentary Override Safety is checked", channel);
    }
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

// Momentary override, shared by both places it appears (the AUX-N
// momentary function and the SKM page-2 momentary keys, using the same
// per-channel state so pressing either one for the same channel behaves
// consistently): pressing it inverts the relay's current state; releasing
// restores whatever the state was immediately before the press. So if a
// channel is latched ON and this is pressed, it goes OFF while held and
// back ON on release -- and symmetrically for a channel that's OFF, it
// goes ON while held and back OFF on release. This is an override, not a
// direct setter, specifically so it can coexist with the toggle variant /
// SKM on the same channel without fighting over it (an earlier version
// mirrored the input value straight to the relay, but AUX-N input devices
// report status periodically even while idle, so its own idle "released"
// reports kept silently overriding whatever the toggle variant had set --
// see docs/vt-ui-design.md#aux-n-functions-17-total).
//
// This is also the one path that can bypass the DI interlock, gated by
// g_momentary_override_safety_enabled: e.g. an auto-mode that stops a
// hydraulic cylinder at a DI-triggered limit partway through its travel,
// where the operator sometimes deliberately needs to push past it (see
// docs/vt-ui-design.md). Only the rising-edge (press) call needs the
// bypass -- restoring on release only ever turns a channel OFF or back to
// whatever it legitimately was already, neither of which the interlock
// blocks anyway.
struct MomentaryOverrideState {
    bool last_input_state = false;
    bool saved_state_before_press = false;
};
MomentaryOverrideState g_momentary_state[9];  // index 1-8, [0] unused

void handle_momentary_override(int channel, bool pressed) {
    MomentaryOverrideState& st = g_momentary_state[channel];
    if (pressed && !st.last_input_state) {
        // Rising edge: remember the current state, invert it.
        st.saved_state_before_press = io::relay_driver::get_relay(channel);
        apply_relay_state(channel, !st.saved_state_before_press, g_momentary_override_safety_enabled);
    } else if (!pressed && st.last_input_state) {
        // Falling edge: restore.
        if (io::relay_driver::get_relay(channel) != st.saved_state_before_press) {
            apply_relay_state(channel, st.saved_state_before_press);
        }
    }
    st.last_input_state = pressed;
}

void handle_soft_key_event(const isobus::VirtualTerminalClient::VTKeyEvent& event) {
    // Page 2's momentary keys need press AND release (to invert-then-
    // restore), unlike everything below which only acts on release.
    for (int ch = 1; ch <= 8; ++ch) {
        if (event.objectID == object_pool_ids::softkey2_id(ch)) {
            bool pressed = (event.keyEvent == isobus::VirtualTerminalClient::KeyActivationCode::ButtonPressedOrLatched ||
                            event.keyEvent == isobus::VirtualTerminalClient::KeyActivationCode::ButtonStillHeld);
            handle_momentary_override(ch, pressed);
            return;
        }
    }

    if (event.keyEvent != isobus::VirtualTerminalClient::KeyActivationCode::ButtonUnlatchedOrReleased) {
        return;  // everything below (including the back key) acts on release only
    }

    if (event.objectID == object_pool_ids::kSoftkeyBack) {
        bool ok = g_vt_client->send_change_softkey_mask(isobus::VirtualTerminalClient::MaskType::DataMask,
                                                        object_pool_ids::kDataMask, object_pool_ids::kSoftKeyMask);
        ESP_LOGI(kTag, "SK back: switch to page 1 -> %s", ok ? "sent" : "FAILED to send");
        return;
    }

    if (event.objectID == object_pool_ids::kSoftkeyNext3) {
        bool ok = g_vt_client->send_change_softkey_mask(isobus::VirtualTerminalClient::MaskType::DataMask,
                                                        object_pool_ids::kDataMask, object_pool_ids::kSoftKeyMask3);
        ESP_LOGI(kTag, "SK next: switch to page 3 (WiFi) -> %s", ok ? "sent" : "FAILED to send");
        return;
    }

    if (event.objectID == object_pool_ids::kSoftkeyBack3) {
        bool ok = g_vt_client->send_change_softkey_mask(isobus::VirtualTerminalClient::MaskType::DataMask,
                                                        object_pool_ids::kDataMask, object_pool_ids::kSoftKeyMask2);
        ESP_LOGI(kTag, "SK back: switch to page 2 -> %s", ok ? "sent" : "FAILED to send");
        return;
    }

    if (event.objectID == object_pool_ids::kSoftkeyWifiToggle) {
        bool new_enabled = !net::wifi_ap::is_enabled();
        net::wifi_ap::set_enabled(new_enabled);
        g_vt_client->send_change_fill_attributes(
            object_pool_ids::kWifiEnabledFillAttr,
            new_enabled ? isobus::VirtualTerminalClient::FillType::FillWithSpecifiedColourInFillColourAttribute
                        : isobus::VirtualTerminalClient::FillType::NoFill,
            kColourBlack, isobus::NULL_OBJECT_ID);
        return;
    }

    if (event.objectID == object_pool_ids::kSoftkeyOverrideToggle) {
        g_momentary_override_safety_enabled = !g_momentary_override_safety_enabled;
        ESP_LOGI(kTag, "Momentary Override Safety: %s", g_momentary_override_safety_enabled ? "CHECKED (momentary can bypass DI interlock)" : "unchecked (default)");
        g_vt_client->send_change_fill_attributes(
            object_pool_ids::kOverrideCheckboxFillAttr,
            g_momentary_override_safety_enabled ? isobus::VirtualTerminalClient::FillType::FillWithSpecifiedColourInFillColourAttribute
                                                 : isobus::VirtualTerminalClient::FillType::NoFill,
            kColourBlack, isobus::NULL_OBJECT_ID);
        return;
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
        return;
    }

    if (event.objectID == object_pool_ids::softkey_id(10)) {
        bool ok = g_vt_client->send_change_softkey_mask(isobus::VirtualTerminalClient::MaskType::DataMask,
                                                        object_pool_ids::kDataMask, object_pool_ids::kSoftKeyMask2);
        ESP_LOGI(kTag, "SK10: switch to page 2 -> %s", ok ? "sent" : "FAILED to send");
    }
}

// Diagnostic only: confirms whether the VT actually applied a soft key
// mask change (vs. our send call merely succeeding at the CAN-transmit
// level) -- useful for telling apart "we never sent it" from "we sent it
// but the VT rejected/ignored it".
void handle_change_soft_key_mask_event(const isobus::VirtualTerminalClient::VTChangeSoftKeyMaskEvent& event) {
    ESP_LOGI(kTag, "VT confirms soft key mask now %u (mask %u) missingObjects=%d maskOrChildHasErrors=%d anyOtherError=%d",
             event.softKeyMaskObjectID, event.dataOrAlarmMaskObjectID, event.missingObjects,
             event.maskOrChildHasErrors, event.anyOtherError);
}

// Fired when the operator edits the WiFi password Input String and
// confirms it on the VT (see docs/vt-ui-design.md#wifi-status--control-panel).
// The VT always reports the *entire* field content, space-padded to its
// fixed reserved length, not just what changed -- trim trailing padding to
// get the password the operator actually intended.
void handle_change_string_value_event(const isobus::VirtualTerminalClient::VTChangeStringValueEvent& event) {
    if (event.objectID != object_pool_ids::kWifiPasswordInput) {
        return;
    }
    std::string password = event.value;
    while (!password.empty() && password.back() == ' ') {
        password.pop_back();
    }
    if (net::wifi_ap::set_password(password)) {
        ESP_LOGI(kTag, "WiFi AP password changed from the VT panel");
    } else {
        ESP_LOGW(kTag, "WiFi AP password change rejected (needs at least 8 characters) -- reverting the displayed value");
    }
    // Re-push the actual current password either way: on success this just
    // re-pads it to the field's fixed width the same way it started; on
    // rejection this undoes what the operator just typed, since
    // net::wifi_ap::set_password() left the real password unchanged.
    std::string display = net::wifi_ap::get_password();
    display.resize(object_pool_ids::kWifiPasswordMaxChars, ' ');
    g_vt_client->send_change_string_value(object_pool_ids::kWifiPasswordInput, display);
}

// AUX-N: both function variants per channel are declared non-latching/
// momentary at the protocol level (most tractors only expose momentary
// physical buttons, and a tractor's assignment menu generally only offers
// type-matched input/function pairs -- see the comment in
// gen_object_pool.py's build_pool() for why). The "latching" *result* for
// the toggle variant is therefore produced here, in firmware, not by the
// declared function type: toggle on each rising edge, ignore the release.
// The other variant uses handle_momentary_override() above.
void handle_aux_function_event(const isobus::VirtualTerminalClient::AuxiliaryFunctionEvent& event) {
    const bool state = (event.value1 != 0);
    const uint16_t function_id = event.function.functionObjectID;

    for (int ch = 1; ch <= 8; ++ch) {
        if (function_id == object_pool_ids::aux_latch_function_id(ch)) {
            // Edge-triggered toggle: flip the relay on press, do nothing
            // on release, so a momentary button acts like a latch.
            static bool last_latch_input_state[9] = {};  // index 1-8, [0] unused
            if (state && !last_latch_input_state[ch]) {
                apply_relay_state(ch, !io::relay_driver::get_relay(ch));
            }
            last_latch_input_state[ch] = state;
            return;
        }
        if (function_id == object_pool_ids::aux_momentary_function_id(ch)) {
            handle_momentary_override(ch, state);
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

void set_interlock_state(int channel, bool di_active) {
    if (!g_vt_client) {
        return;  // VT client not started yet, nothing to reflect
    }

    // Force the relay off the moment its paired DI activates. apply_relay_state
    // itself now refuses ON requests while disabled, so this only ever
    // needs to push a channel *off*, never on.
    if (di_active && io::relay_driver::get_relay(channel)) {
        apply_relay_state(channel, false);
    }

    g_vt_client->send_change_fill_attributes(
        object_pool_ids::di_fill_attr_id(channel),
        di_active ? isobus::VirtualTerminalClient::FillType::FillWithSpecifiedColourInFillColourAttribute
                   : isobus::VirtualTerminalClient::FillType::NoFill,
        kColourBlack, isobus::NULL_OBJECT_ID);

    // "!" marks the channel as disabled directly on its own Data Mask
    // label, so it's obvious at a glance why a channel won't respond,
    // without needing to look at the (smaller, further away) DI indicator.
    std::string label = "R" + std::to_string(channel) + (di_active ? "!" : "");
    g_vt_client->send_change_string_value(object_pool_ids::relay_label_id(channel), label);
}

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
    g_vt_partner = vt_partner;

    const uint32_t pool_size = static_cast<uint32_t>(object_pool_iop_end - object_pool_iop_start);
    // Content-hashed, not hand-bumped: the VT caches pools by this label,
    // so a stale hardcoded string here would make it silently keep serving
    // an old cached pool after we change the generator.
    //
    // A non-empty label here makes the client ask the VT (Get Versions)
    // whether it already has this pool cached before uploading, and that
    // exchange turned out to have no fallback if the VT never answers --
    // observed as a permanent connection stall (see
    // docs/roadmap.md#phase-3--minimal-vt-presence). That's judged to be a
    // VT-side bug to fix in AgIsoVirtualTerminal itself (same suspected
    // class of bug as an AUX-N assignment issue seen separately), not
    // something to route around here -- pool caching is the correct
    // long-term behavior once that's fixed, so keeping it rather than
    // permanently forcing a re-upload from this side.
    const std::string pool_version =
        isobus::IOPFileInterface::hash_object_pool_to_version(object_pool_iop_start, pool_size);

    g_vt_client = std::make_shared<isobus::VirtualTerminalClient>(vt_partner, internal_ecu);
    g_vt_client->set_object_pool(0, object_pool_iop_start, pool_size, pool_version);
    g_vt_client->get_vt_soft_key_event_dispatcher().add_listener(handle_soft_key_event);
    g_vt_client->get_auxiliary_function_event_dispatcher().add_listener(handle_aux_function_event);
    g_vt_client->get_vt_change_soft_key_mask_event_dispatcher().add_listener(handle_change_soft_key_mask_event);
    g_vt_client->get_vt_change_string_value_event_dispatcher().add_listener(handle_change_string_value_event);
    g_vt_client->initialize(true);
    ESP_LOGI(kTag, "VT client started, waiting for a Virtual Terminal on the bus...");
}

bool is_connected() {
    return g_vt_client && g_vt_client->get_is_connected();
}

// Distinguishes "no VT has claimed an address matching our NAME filter yet"
// (nothing to connect to -- a bus/wiring/NAME-filter problem) from "a VT is
// present but the connection handshake itself isn't completing" (a
// protocol-level problem). VirtualTerminalClient's own state machine won't
// even leave StateMachineState::Disconnected until this is true, regardless
// of whether the VT is broadcasting VT Status (see update()'s
// `if (nullptr != partnerControlFunction)` / `get_address_valid()` guards
// in isobus_virtual_terminal_client.cpp).
bool is_partner_claimed() {
    return g_vt_partner && g_vt_partner->get_address_valid();
}

bool set_relay_remote(int channel, bool state) {
    return apply_relay_state(channel, state);
}

// A fresh VT connection means a fresh object pool upload, which resets
// every fill/label to the static pool's defaults -- but our own state
// (relay outputs, DI interlock status, the override checkbox) isn't
// reset by a reconnect, only by an actual firmware reboot. Without this,
// a VT-side hiccup and reconnect (not a power cycle) would leave the
// screen showing all-off/all-unchecked while the real state underneath
// disagreed, until the next state *change* happened to correct it.
// Re-pushes everything unconditionally, reusing the same functions any
// state change already goes through (each is safe to call with an
// unchanged value -- apply_relay_state's I2C write and
// set_interlock_state's force-off check are both no-ops in that case,
// they just also unconditionally resend the VT-facing fill/label either
// way, which is exactly what's needed here).
void resync_display() {
    if (!g_vt_client) {
        return;
    }

    // The object pool ships with a static "AgIsoRelayBlock" title (the
    // build version isn't known at pool-generation time) -- overwrite it
    // with esp_app_get_description()->version (ESP-IDF's automatic
    // `git describe --always --dirty`), so which exact firmware build is
    // running is visible on the VT screen itself, not just a serial log.
    std::string title = std::string("AgIsoRelayBlock ") + esp_app_get_description()->version;
    // Only truncate if needed -- a shorter string is safe to send as-is
    // (the VT pads it with spaces to the object's existing length itself,
    // per ISO 11783-6), but sending more characters than the object pool
    // reserved (object_pool_ids::kTitleStringMaxChars) would overrun what
    // some VTs treat as a fixed-size field.
    if (title.size() > object_pool_ids::kTitleStringMaxChars) {
        title.resize(object_pool_ids::kTitleStringMaxChars);
    }
    g_vt_client->send_change_string_value(object_pool_ids::kTitleString, title);

    for (int ch = 1; ch <= 8; ++ch) {
        apply_relay_state(ch, io::relay_driver::get_relay(ch));
        set_interlock_state(ch, automation::interlock::is_disabled(ch));
    }
    g_vt_client->send_change_fill_attributes(
        object_pool_ids::kOverrideCheckboxFillAttr,
        g_momentary_override_safety_enabled ? isobus::VirtualTerminalClient::FillType::FillWithSpecifiedColourInFillColourAttribute
                                             : isobus::VirtualTerminalClient::FillType::NoFill,
        kColourBlack, isobus::NULL_OBJECT_ID);

    g_vt_client->send_change_fill_attributes(
        object_pool_ids::kWifiEnabledFillAttr,
        net::wifi_ap::is_enabled() ? isobus::VirtualTerminalClient::FillType::FillWithSpecifiedColourInFillColourAttribute
                                    : isobus::VirtualTerminalClient::FillType::NoFill,
        kColourBlack, isobus::NULL_OBJECT_ID);
    g_vt_client->send_change_string_value(object_pool_ids::kWifiSsidLabel, "SSID: " + net::wifi_ap::get_ssid());
    std::string password_display = net::wifi_ap::get_password();
    password_display.resize(object_pool_ids::kWifiPasswordMaxChars, ' ');
    g_vt_client->send_change_string_value(object_pool_ids::kWifiPasswordInput, password_display);
    // Always 192.168.4.1 -- ESP-IDF's fixed default AP-mode address, not
    // configured otherwise. Sent dynamically anyway (rather than baked
    // into the static pool) for the same reason the title's version isn't:
    // one clearly-labeled place this comes from, not two that could drift.
    g_vt_client->send_change_string_value(object_pool_ids::kWifiIpLabel, "IP: 192.168.4.1");
    refresh_wifi_client_count();
}

// The connected-client count can change at any moment (someone's phone
// joining or leaving the AP), not just around a VT connect/reconnect --
// call periodically (see app_main.cpp's main loop) as well as from
// resync_display(). Only sends a Change String Value when the count
// actually changed, same diffing pattern as automation::interlock's own
// update() loop, so this doesn't put a CAN message on the bus every tick
// for no reason.
void refresh_wifi_client_count() {
    if (!g_vt_client) {
        return;
    }
    static uint8_t last_count = 0xFF;  // force the first call through
    uint8_t count = net::wifi_ap::get_connected_client_count();
    if (count == last_count) {
        return;
    }
    last_count = count;
    g_vt_client->send_change_string_value(object_pool_ids::kWifiClientsLabel, "Clients: " + std::to_string(count));
}

}  // namespace iso::vt_app
