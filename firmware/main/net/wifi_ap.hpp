#pragma once

#include <cstdint>
#include <string>

// Phase 7 (docs/roadmap.md#phase-7--wifi-ap--ota, docs/architecture.md#wifi-ap--ota-planned):
// always-on-by-default SoftAP, independent of Ethernet/station state, so
// there's normally always a way to reach the device for local config/OTA
// -- even freshly unboxed or mounted somewhere without a network drop.
// "Always-on by default" because it's also operator-disable-able from the
// VT (see docs/vt-ui-design.md's WiFi panel) for whoever wants it off in
// the field once setup is done -- disabling it means no local
// web/OTA/relay-control reachability at all until re-enabled, same as if
// this code never ran; there's no Ethernet fallback wired up yet.
namespace net::wifi_ap {

// Initializes NVS (required by the WiFi driver, and where the AP password
// is persisted), brings up a SoftAP named "AgIsoBlock-XXXX" (XXXX = the
// last 2 bytes of the station MAC), and logs the SSID/password once at
// INFO level (every boot, not just first). The password is generated once
// on first boot and persisted to NVS from then on (never a fixed/shared
// default -- requirement N7); get_password()/set_password() below let the
// VT panel display and change it after that (see
// docs/vt-ui-design.md#wifi-status--control-panel) rather than requiring
// a serial connection for anything past the very first setup. Call once,
// early in app_main -- must run before net::web_server::init().
void init();

// True unless the operator has explicitly disabled the AP from the VT
// panel. Starts true (the AP comes up in init() unconditionally -- there's
// no persisted "start disabled" state, matching the same don't-persist-
// bypassed-safety-state reasoning as the Momentary Override Safety
// checkbox: a device that's unreachable over WiFi because someone
// disabled it last session and then power-cycled would be a confusing,
// hard-to-diagnose-remotely state to wake up in).
bool is_enabled();

// Stops (tearing down the AP interface, dropping any connected clients --
// including whatever loaded the web UI/VT panel that requested this, if
// it was WiFi-connected rather than Ethernet) or restarts the SoftAP.
// No-op if already in the requested state.
void set_enabled(bool enabled);

// Current SSID/password, e.g. for displaying on the VT panel. Password is
// returned in the clear -- this project has no concept of a "hide it"
// mode; anyone with physical/VT access to the device already has
// equivalent access to everything the AP protects.
std::string get_ssid();
std::string get_password();

// Changes the AP password (persists to NVS, then reconfigures the running
// AP so it takes effect immediately -- existing WiFi clients, including
// possibly whoever just made this change over WiFi, are disconnected and
// must reconnect with the new password). Rejects (returns false, leaves
// the password unchanged) anything shorter than 8 characters -- WPA2-PSK's
// hard minimum, not a policy choice; the VT panel is expected to have
// already caught this and shown its own message, this is the actual
// enforcement point.
bool set_password(const std::string& password);

// Number of stations currently associated to the AP -- for the VT panel's
// "N connected" display. 0 whenever the AP itself is disabled.
uint8_t get_connected_client_count();

}  // namespace net::wifi_ap
